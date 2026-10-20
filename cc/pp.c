/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "cc.h"

typedef struct Macro Macro;
struct Macro {
	char *name;
	int fn;
	char *params[64];
	int nparams;
	int variadic;
	Tok *body;
	int active;
	int builtin;
	Macro *next;
};

typedef struct Frame Frame;
struct Frame {
	Tok *tok;
	Macro *mac;
	int file;
	Frame *next;
};

typedef struct Cond Cond;
struct Cond {
	int taking;
	int done;
	int in_else;
	int outer;
	Tok *tok;
	Cond *next;
};

typedef struct {
	Frame *frames;
	Tok *back;
} Input;

#define MHASH 1024

char *incdirs[32];
int nincdirs;

static Macro *macros[MHASH];
static Input *in;
static Cond *conds;
static int include_depth;

static unsigned hashs(const char *s) {
	unsigned h = 5381;
	while (*s) h = h * 33 + (unsigned char)*s++;
	return h % MHASH;
}

static Macro *find_macro(const char *name) {
	Macro *m;
	for (m = macros[hashs(name)]; m; m = m->next)
		if (strcmp(m->name, name) == 0) return m;
	return NULL;
}

static void undef_macro(const char *name) {
	Macro **pp = &macros[hashs(name)];
	while (*pp) {
		if (strcmp((*pp)->name, name) == 0) {
			*pp = (*pp)->next;
			return;
		}
		pp = &(*pp)->next;
	}
}

static Macro *add_macro(const char *name) {
	Macro *m = xmalloc(sizeof(Macro));
	unsigned h = hashs(name);
	undef_macro(name);
	m->name = xstrdup(name);
	m->next = macros[h];
	macros[h] = m;
	return m;
}

static Tok *new_tok(int kind, Tok *from) {
	Tok *t = xmalloc(sizeof(Tok));
	t->kind = kind;
	if (from) {
		t->file = from->file;
		t->line = from->line;
	}
	return t;
}

static Tok *copy_tok(Tok *t) {
	Tok *n = xmalloc(sizeof(Tok));
	*n = *t;
	n->next = NULL;
	return n;
}

static char *read_file(const char *path, int *len) {
	FILE *f = fopen(path, "rb");
	char *buf;
	int cap = 65536, n = 0, r;
	if (!f) return NULL;
	buf = xmalloc(cap + 2);
	while ((r = (int)fread(buf + n, 1, cap - n, f)) > 0) {
		n += r;
		if (n == cap) {
			char *nb;
			cap *= 2;
			nb = xmalloc(cap + 2);
			memcpy(nb, buf, n);
			free(buf);
			buf = nb;
		}
	}
	fclose(f);
	if (n > 0 && buf[n - 1] != '\n') buf[n++] = '\n';
	buf[n] = 0;
	*len = n;
	return buf;
}

static const char *puncts[] = {
	"<<=", ">>=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
	"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##", NULL
};

static int read_escape(char **pp, Tok *t) {
	char *p = *pp;
	int c = (unsigned char)*p++;
	if (c != '\\') {
		*pp = p;
		return c;
	}
	c = (unsigned char)*p++;
	switch (c) {
	case 'n': c = '\n'; break;
	case 't': c = '\t'; break;
	case 'r': c = '\r'; break;
	case 'a': c = 7; break;
	case 'b': c = 8; break;
	case 'f': c = 12; break;
	case 'v': c = 11; break;
	case 'e': c = 27; break;
	case '\\': case '\'': case '"': case '?': break;
	case 'x':
		c = 0;
		if (!isxdigit((unsigned char)*p)) error_at(t, "bad \\x escape");
		while (isxdigit((unsigned char)*p)) {
			int d = isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10);
			c = c * 16 + d;
			p++;
		}
		c &= 0xFF;
		break;
	default:
		if (c >= '0' && c <= '7') {
			int k = 1;
			c -= '0';
			while (k < 3 && *p >= '0' && *p <= '7') {
				c = c * 8 + (*p++ - '0');
				k++;
			}
			c &= 0xFF;
		} else {
			error_at(t, "unknown escape sequence '\\%c'", c);
		}
	}
	*pp = p;
	return c;
}

static void parse_number(Tok *t) {
	char *p = t->text;
	unsigned v = 0;
	int base = 10, overflow = 0;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
	} else if (p[0] == '0') {
		base = 8;
	}
	while (*p) {
		int c = tolower((unsigned char)*p), d;
		if (isdigit(c)) d = c - '0';
		else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else break;
		if (d >= base) error_at(t, "invalid digit in number '%s'", t->text);
		if (v > (0xFFFFFFFFu - d) / base) overflow = 1;
		v = v * base + d;
		p++;
	}
	if (overflow) error_at(t, "integer constant too large");
	while (*p) {
		int c = tolower((unsigned char)*p);
		if (c == 'u') t->uns = 1;
		else if (c != 'l') error_at(t, "invalid number '%s'", t->text);
		p++;
	}
	t->val = v;
	if (v > 0x7FFFFFFF) t->uns = 1;
}

static Tok *tokenize(char *src, char *file) {
	Tok head, *cur = &head;
	char *p = src;
	int line = 1, bol = 1, space = 0;
	head.next = NULL;
	while (*p) {
		Tok *t;
		char *s;
		if (*p == '\n') {
			line++;
			p++;
			bol = 1;
			space = 0;
			continue;
		}
		if (*p == '\\' && p[1] == '\n') {
			line++;
			p += 2;
			space = 1;
			continue;
		}
		if (*p == '\\' && p[1] == '\r' && p[2] == '\n') {
			line++;
			p += 3;
			space = 1;
			continue;
		}
		if (isspace((unsigned char)*p)) {
			p++;
			space = 1;
			continue;
		}
		if (p[0] == '/' && p[1] == '/') {
			while (*p && *p != '\n') p++;
			space = 1;
			continue;
		}
		if (p[0] == '/' && p[1] == '*') {
			int sl = line;
			p += 2;
			while (*p && !(p[0] == '*' && p[1] == '/')) {
				if (*p == '\n') line++;
				p++;
			}
			if (!*p) {
				Tok e;
				memset(&e, 0, sizeof(e));
				e.file = file;
				e.line = sl;
				error_at(&e, "unterminated comment");
			}
			p += 2;
			space = 1;
			continue;
		}
		t = xmalloc(sizeof(Tok));
		t->file = file;
		t->line = line;
		t->bol = bol;
		t->space = space;
		bol = 0;
		space = 0;
		s = p;
		if (isalpha((unsigned char)*p) || *p == '_' || *p == '$') {
			while (isalnum((unsigned char)*p) || *p == '_' || *p == '$') p++;
			t->kind = T_IDENT;
		} else if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
			while (isalnum((unsigned char)*p) || *p == '.' || *p == '_') p++;
			t->kind = T_NUM;
		} else if (*p == '"' || *p == '\'') {
			char q = *p++;
			int n = 0, cap = 64;
			char *buf = xmalloc(cap);
			t->text = NULL;
			while (*p != q) {
				int c;
				if (*p == '\n' || *p == 0) {
					t->len = -1;
					break;
				}
				c = read_escape(&p, t);
				if (n + 1 >= cap) {
					char *nb = xmalloc(cap * 2);
					memcpy(nb, buf, n);
					free(buf);
					buf = nb;
					cap *= 2;
				}
				buf[n++] = (char)c;
			}
			buf[n] = 0;
			if (t->len == -1) {
				t->kind = T_PUNCT;
				t->str = q == '"' ? "unterminated string" : "unterminated character constant";
			} else if (q == '"') {
				p++;
				t->kind = T_STR;
				t->str = buf;
				t->len = n;
			} else {
				p++;
				t->kind = T_CHAR;
				t->val = (unsigned)(int)(signed char)buf[0];
				if (n != 1) {
					t->kind = T_PUNCT;
					t->len = -1;
					t->str = n ? "multi-character constant" : "empty character constant";
				}
			}
		} else {
			int i, k = 0;
			for (i = 0; puncts[i]; i++) {
				int l = (int)strlen(puncts[i]);
				if (strncmp(p, puncts[i], l) == 0) {
					k = l;
					break;
				}
			}
			if (!k) {
				if (!strchr("+-*/%&|^~!=<>?:;,.()[]{}#", *p)) {
					t->len = -1;
					t->str = "invalid character";
				}
				k = 1;
			}
			p += k;
			t->kind = T_PUNCT;
		}
		t->text = xmalloc((int)(p - s) + 1);
		memcpy(t->text, s, p - s);
		t->text[p - s] = 0;
		if (t->kind == T_NUM) parse_number(t);
		cur->next = t;
		cur = t;
	}
	cur->next = new_tok(T_EOF, NULL);
	cur->next->file = file;
	cur->next->line = line;
	cur->next->bol = 1;
	cur->next->text = "";
	return head.next;
}

static int is_punct(Tok *t, const char *s) {
	return t && t->kind == T_PUNCT && strcmp(t->text, s) == 0;
}

static int is_ident(Tok *t, const char *s) {
	return t && t->kind == T_IDENT && strcmp(t->text, s) == 0;
}

static void push_frame(Tok *list, Macro *m, int file) {
	Frame *f = xmalloc(sizeof(Frame));
	f->tok = list;
	f->mac = m;
	f->file = file;
	f->next = in->frames;
	in->frames = f;
	if (m) m->active = 1;
}

static Tok *raw_next(void) {
	Tok *t;
	if (in->back) {
		t = in->back;
		in->back = t->next;
		t->next = NULL;
		return t;
	}
	while (in->frames) {
		Frame *f = in->frames;
		t = f->tok;
		if (t && !(f->file && t->kind == T_EOF)) {
			f->tok = t->next;
			return t;
		}
		if (f->file && include_depth == 0 && !f->next) return t;
		if (f->file) include_depth--;
		in->frames = f->next;
		if (f->mac) f->mac->active = 0;
	}
	return NULL;
}

static void unget(Tok *t) {
	t->next = in->back;
	in->back = t;
}

static Frame *top_file(void) {
	Frame *f = in->frames;
	while (f && !f->file) f = f->next;
	return f;
}

static Tok *line_tokens(void) {
	Frame *f = in->frames;
	Tok head, *cur = &head;
	head.next = NULL;
	while (f->tok && !f->tok->bol && f->tok->kind != T_EOF) {
		cur->next = copy_tok(f->tok);
		cur = cur->next;
		f->tok = f->tok->next;
	}
	return head.next;
}

static void skip_line(void) {
	Frame *f = in->frames;
	while (f->tok && !f->tok->bol && f->tok->kind != T_EOF) f->tok = f->tok->next;
}

static char *dir_of(const char *path) {
	const char *s = strrchr(path, '/');
	char *d;
	if (!s) return xstrdup("");
	d = xmalloc((int)(s - path) + 2);
	memcpy(d, path, s - path + 1);
	d[s - path + 1] = 0;
	return d;
}

static int file_exists(const char *p) {
	FILE *f = fopen(p, "r");
	if (!f) return 0;
	fclose(f);
	return 1;
}

static void include_file(char *path, Tok *at) {
	int len;
	char *src;
	Tok *toks;
	if (include_depth > 64) error_at(at, "#include nested too deeply");
	src = read_file(path, &len);
	if (!src) error_at(at, "cannot open include file '%s'", path);
	toks = tokenize(src, xstrdup(path));
	include_depth++;
	push_frame(toks, NULL, 1);
}

static void do_include(Tok *dir) {
	Tok *t = line_tokens();
	char name[512], path[1024];
	int sys = 0, i;
	if (!t) error_at(dir, "#include expects a file name");
	if (t->kind == T_STR) {
		if ((int)strlen(t->str) >= 500) error_at(t, "file name too long");
		strcpy(name, t->str);
	} else if (is_punct(t, "<")) {
		name[0] = 0;
		sys = 1;
		for (t = t->next; t && !is_punct(t, ">"); t = t->next) {
			if (strlen(name) + strlen(t->text) >= 500) error_at(dir, "file name too long");
			strcat(name, t->text);
		}
		if (!t) error_at(dir, "missing '>' in #include");
	} else {
		error_at(t, "#include expects \"file\" or <file>");
	}
	if (name[0] == '/') {
		include_file(name, dir);
		return;
	}
	if (!sys) {
		char *d = dir_of(dir->file);
		snprintf(path, sizeof(path), "%s%s", d, name);
		if (file_exists(path)) {
			include_file(path, dir);
			return;
		}
	}
	for (i = 0; i < nincdirs; i++) {
		snprintf(path, sizeof(path), "%s/%s", incdirs[i], name);
		if (file_exists(path)) {
			include_file(path, dir);
			return;
		}
	}
	error_at(dir, "cannot find include file '%s'", name);
}

static void do_define(Tok *dir) {
	Frame *f = in->frames;
	Tok *name = f->tok, *t;
	Macro *m;
	Tok head, *cur = &head;
	if (!name || name->bol || name->kind != T_IDENT) error_at(dir, "macro name expected");
	f->tok = name->next;
	m = add_macro(name->text);
	t = f->tok;
	if (t && !t->bol && is_punct(t, "(") && !t->space) {
		m->fn = 1;
		f->tok = t->next;
		t = f->tok;
		if (is_punct(t, ")")) {
			f->tok = t->next;
		} else {
			while (1) {
				t = f->tok;
				if (!t || t->bol) error_at(dir, "unterminated macro parameter list");
				if (is_punct(t, "...")) {
					m->variadic = 1;
					m->params[m->nparams++] = "__VA_ARGS__";
					f->tok = t->next;
					t = f->tok;
					if (!is_punct(t, ")") || t->bol) error_at(dir, "')' expected after '...'");
					f->tok = t->next;
					break;
				}
				if (t->kind != T_IDENT) error_at(t, "macro parameter name expected");
				if (m->nparams >= 63) error_at(t, "too many macro parameters");
				m->params[m->nparams++] = t->text;
				f->tok = t->next;
				t = f->tok;
				if (t && !t->bol && is_punct(t, ",")) {
					f->tok = t->next;
					continue;
				}
				if (t && !t->bol && is_punct(t, ")")) {
					f->tok = t->next;
					break;
				}
				error_at(dir, "bad macro parameter list");
			}
		}
	}
	head.next = NULL;
	while (f->tok && !f->tok->bol && f->tok->kind != T_EOF) {
		cur->next = copy_tok(f->tok);
		cur = cur->next;
		f->tok = f->tok->next;
	}
	m->body = head.next;
}

static Tok *expand_list(Tok *list);

static Tok *num_tok(unsigned v, Tok *at) {
	Tok *t = new_tok(T_NUM, at);
	t->val = v;
	t->text = fmt("%u", v);
	return t;
}

static long eval_expr(Tok **pp, Tok *dir);

static long eval_primary(Tok **pp, Tok *dir) {
	Tok *t = *pp;
	long v;
	if (!t) error_at(dir, "missing expression in #if");
	if (is_punct(t, "(")) {
		*pp = t->next;
		v = eval_expr(pp, dir);
		if (!is_punct(*pp, ")")) error_at(dir, "missing ')' in #if");
		*pp = (*pp)->next;
		return v;
	}
	if (is_punct(t, "!")) {
		*pp = t->next;
		return !eval_primary(pp, dir);
	}
	if (is_punct(t, "~")) {
		*pp = t->next;
		return ~eval_primary(pp, dir);
	}
	if (is_punct(t, "-")) {
		*pp = t->next;
		return -eval_primary(pp, dir);
	}
	if (is_punct(t, "+")) {
		*pp = t->next;
		return eval_primary(pp, dir);
	}
	if (t->kind == T_NUM || t->kind == T_CHAR) {
		*pp = t->next;
		return (long)(int)t->val;
	}
	if (t->kind == T_IDENT) {
		*pp = t->next;
		return 0;
	}
	error_at(t, "bad token '%s' in #if", t->text);
	return 0;
}

static int prec_of(Tok *t) {
	static const char *ops[] = {"*", "/", "%", "+", "-", "<<", ">>", "<", ">", "<=", ">=", "==", "!=", "&", "^", "|", "&&", "||", NULL};
	static const int pr[] = {10, 10, 10, 9, 9, 8, 8, 7, 7, 7, 7, 6, 6, 5, 4, 3, 2, 1};
	int i;
	if (!t || t->kind != T_PUNCT) return -1;
	for (i = 0; ops[i]; i++)
		if (strcmp(t->text, ops[i]) == 0) return pr[i];
	return -1;
}

static long eval_bin(Tok **pp, Tok *dir, int minp) {
	long l = eval_primary(pp, dir);
	while (1) {
		Tok *op = *pp;
		int p = prec_of(op);
		long r;
		if (p < minp) return l;
		*pp = op->next;
		r = eval_bin(pp, dir, p + 1);
		switch (op->text[0]) {
		case '*': l = l * r; break;
		case '/': if (!r) error_at(dir, "division by zero in #if"); l = l / r; break;
		case '%': if (!r) error_at(dir, "division by zero in #if"); l = l % r; break;
		case '+': l = l + r; break;
		case '-': l = l - r; break;
		case '<':
			if (op->text[1] == '<') l = l << r;
			else if (op->text[1] == '=') l = l <= r;
			else l = l < r;
			break;
		case '>':
			if (op->text[1] == '>') l = l >> r;
			else if (op->text[1] == '=') l = l >= r;
			else l = l > r;
			break;
		case '=': l = l == r; break;
		case '!': l = l != r; break;
		case '&': l = op->text[1] ? (l && r) : (l & r); break;
		case '^': l = l ^ r; break;
		case '|': l = op->text[1] ? (l || r) : (l | r); break;
		}
	}
}

static long eval_expr(Tok **pp, Tok *dir) {
	long c = eval_bin(pp, dir, 1);
	if (is_punct(*pp, "?")) {
		long a, b;
		*pp = (*pp)->next;
		a = eval_expr(pp, dir);
		if (!is_punct(*pp, ":")) error_at(dir, "missing ':' in #if");
		*pp = (*pp)->next;
		b = eval_expr(pp, dir);
		return c ? a : b;
	}
	return c;
}

static int eval_if(Tok *dir) {
	Tok *t = line_tokens(), head, *cur = &head, *e;
	long v;
	head.next = NULL;
	while (t) {
		Tok *nx = t->next;
		if (is_ident(t, "defined")) {
			Tok *n = nx;
			int paren = 0;
			if (is_punct(n, "(")) {
				paren = 1;
				n = n->next;
			}
			if (!n || n->kind != T_IDENT) error_at(dir, "macro name expected after defined");
			cur->next = num_tok(find_macro(n->text) ? 1 : 0, t);
			cur = cur->next;
			n = n->next;
			if (paren) {
				if (!is_punct(n, ")")) error_at(dir, "missing ')' after defined");
				n = n->next;
			}
			t = n;
			continue;
		}
		t->next = NULL;
		cur->next = t;
		cur = t;
		t = nx;
	}
	e = expand_list(head.next);
	if (!e) error_at(dir, "missing expression in #if");
	v = eval_expr(&e, dir);
	if (e) error_at(dir, "extra tokens in #if expression");
	return v != 0;
}

static int skipping(void) {
	return conds && !conds->taking;
}

static void directive(Tok *hash) {
	Frame *f = in->frames;
	Tok *d = f->tok;
	Cond *c;
	if (!d || d->bol || d->kind == T_EOF) return;
	f->tok = d->next;
	if (is_ident(d, "ifdef") || is_ident(d, "ifndef") || is_ident(d, "if")) {
		int v = 0;
		c = xmalloc(sizeof(Cond));
		c->outer = !skipping();
		c->tok = d;
		if (c->outer) {
			if (d->text[2] == 0) {
				v = eval_if(d);
			} else {
				Tok *n = f->tok;
				if (!n || n->bol || n->kind != T_IDENT) error_at(d, "macro name expected");
				v = find_macro(n->text) != NULL;
				if (d->text[2] == 'n') v = !v;
				skip_line();
			}
		} else {
			skip_line();
		}
		c->taking = c->outer && v;
		c->done = c->taking;
		c->next = conds;
		conds = c;
		return;
	}
	if (is_ident(d, "elif")) {
		if (!conds) error_at(d, "#elif without #if");
		if (conds->in_else) error_at(d, "#elif after #else");
		if (conds->outer && !conds->done) {
			conds->taking = eval_if(d);
			conds->done = conds->taking;
		} else {
			conds->taking = 0;
			skip_line();
		}
		return;
	}
	if (is_ident(d, "else")) {
		if (!conds) error_at(d, "#else without #if");
		if (conds->in_else) error_at(d, "#else after #else");
		conds->in_else = 1;
		conds->taking = conds->outer && !conds->done;
		conds->done = 1;
		skip_line();
		return;
	}
	if (is_ident(d, "endif")) {
		if (!conds) error_at(d, "#endif without #if");
		conds = conds->next;
		skip_line();
		return;
	}
	if (skipping()) {
		skip_line();
		return;
	}
	if (is_ident(d, "define")) {
		do_define(d);
	} else if (is_ident(d, "undef")) {
		Tok *n = f->tok;
		if (!n || n->bol || n->kind != T_IDENT) error_at(d, "macro name expected");
		undef_macro(n->text);
		skip_line();
	} else if (is_ident(d, "include")) {
		do_include(d);
	} else if (is_ident(d, "error")) {
		Tok *t = line_tokens();
		char msg[1024];
		msg[0] = 0;
		for (; t; t = t->next) {
			if (strlen(msg) + strlen(t->text) + 2 >= sizeof(msg)) break;
			if (msg[0] && t->space) strcat(msg, " ");
			strcat(msg, t->text ? t->text : "");
		}
		error_at(d, "#error %s", msg);
	} else if (is_ident(d, "pragma") || is_ident(d, "line") || is_ident(d, "warning")) {
		skip_line();
	} else {
		error_at(d, "unknown directive '#%s'", d->text);
	}
	(void)hash;
}

static Tok *stringize(Tok *arg, Tok *at) {
	char *buf;
	int n = 0, cap = 256;
	Tok *t;
	buf = xmalloc(cap);
	for (t = arg; t; t = t->next) {
		const char *s;
		char tmp[8];
		int quote = t->kind == T_STR || t->kind == T_CHAR;
		if (t != arg && t->space) buf[n++] = ' ';
		if (t->kind == T_STR) {
			int i;
			buf[n++] = '"';
			for (i = 0; i < t->len; i++) {
				int ch = (unsigned char)t->str[i];
				if (n + 8 >= cap) {
					char *nb = xmalloc(cap * 2);
					memcpy(nb, buf, n);
					free(buf);
					buf = nb;
					cap *= 2;
				}
				if (ch == '"' || ch == '\\') {
					buf[n++] = '\\';
					buf[n++] = (char)ch;
				} else if (ch == '\n') {
					buf[n++] = '\\';
					buf[n++] = 'n';
				} else {
					buf[n++] = (char)ch;
				}
			}
			buf[n++] = '"';
			continue;
		}
		s = t->text;
		if (t->kind == T_CHAR && !s) {
			sprintf(tmp, "%u", t->val);
			s = tmp;
		}
		while (*s) {
			if (n + 4 >= cap) {
				char *nb = xmalloc(cap * 2);
				memcpy(nb, buf, n);
				free(buf);
				buf = nb;
				cap *= 2;
			}
			if (quote && (*s == '"' || *s == '\\')) buf[n++] = '\\';
			buf[n++] = *s++;
		}
	}
	buf[n] = 0;
	t = new_tok(T_STR, at);
	t->str = buf;
	t->len = n;
	t->text = fmt("\"%s\"", buf);
	return t;
}

static Tok *paste(Tok *a, Tok *b) {
	char *s = fmt("%s%s", a->text, b->text);
	Tok *t = tokenize(s, a->file);
	if (t->kind == T_EOF || t->next->kind != T_EOF) error_at(a, "pasting '%s' and '%s' does not give a valid token", a->text, b->text);
	t->line = a->line;
	t->bol = 0;
	t->space = a->space;
	return t;
}

static Tok *copy_list(Tok *l) {
	Tok head, *cur = &head;
	head.next = NULL;
	for (; l; l = l->next) {
		cur->next = copy_tok(l);
		cur = cur->next;
	}
	return head.next;
}

static int param_index(Macro *m, Tok *t) {
	int i;
	if (!t || t->kind != T_IDENT) return -1;
	for (i = 0; i < m->nparams; i++)
		if (strcmp(m->params[i], t->text) == 0) return i;
	return -1;
}

static Tok *substitute(Macro *m, Tok **args, Tok *at) {
	Tok head, *cur = &head, *t;
	Tok *exp[64];
	int i;
	head.next = NULL;
	for (i = 0; i < m->nparams; i++) exp[i] = NULL;
	for (t = m->body; t; t = t->next) {
		int pi;
		if (is_punct(t, "#") && m->fn) {
			pi = param_index(m, t->next);
			if (pi < 0) error_at(t, "'#' is not followed by a macro parameter");
			cur->next = stringize(args[pi], at);
			cur->next->space = t->space;
			cur = cur->next;
			t = t->next;
			continue;
		}
		if (is_punct(t, "##")) {
			Tok *rhs = t->next, *rl;
			if (cur == &head || !rhs) error_at(t, "'##' cannot appear at either end of a macro");
			pi = param_index(m, rhs);
			if (pi >= 0) rl = copy_list(args[pi]);
			else {
				rl = copy_tok(rhs);
			}
			if (rl) {
				Tok *p = paste(cur, rl);
				Tok *prev = &head;
				while (prev->next != cur) prev = prev->next;
				p->next = rl->next;
				prev->next = p;
				cur = p;
				while (cur->next) cur = cur->next;
			}
			t = rhs;
			continue;
		}
		pi = param_index(m, t);
		if (pi >= 0) {
			Tok *l;
			if (t->next && is_punct(t->next, "##")) {
				l = copy_list(args[pi]);
			} else {
				if (!exp[pi]) exp[pi] = expand_list(copy_list(args[pi]));
				l = copy_list(exp[pi]);
			}
			if (l) l->space = t->space;
			cur->next = l;
			while (cur->next) cur = cur->next;
			continue;
		}
		cur->next = copy_tok(t);
		cur = cur->next;
	}
	for (t = head.next; t; t = t->next) {
		t->file = at->file;
		t->line = at->line;
		t->bol = 0;
	}
	return head.next;
}

static Tok *read_args(Macro *m, Tok *name, Tok **args) {
	int n = 0, depth = 0;
	Tok head, *cur = &head, *t;
	head.next = NULL;
	while (1) {
		t = raw_next();
		if (!t || t->kind == T_EOF) error_at(name, "unterminated call to macro '%s'", m->name);
		t = copy_tok(t);
		if (depth == 0 && (is_punct(t, ")") || (is_punct(t, ",") && !(m->variadic && n == m->nparams - 1)))) {
			if (n < 64) args[n] = head.next;
			n++;
			head.next = NULL;
			cur = &head;
			if (is_punct(t, ")")) break;
			continue;
		}
		if (is_punct(t, "(")) depth++;
		if (is_punct(t, ")")) depth--;
		cur->next = t;
		cur = t;
	}
	if (n == 1 && m->nparams == 0 && args[0] == NULL) n = 0;
	if (m->variadic && n == m->nparams - 1) args[n++] = NULL;
	if (n != m->nparams) error_at(name, "macro '%s' expects %d arguments, got %d", m->name, m->nparams, n);
	return t;
}

static int expand(Tok *t) {
	Macro *m = find_macro(t->text);
	Tok *args[64];
	if (!m || t->noexp) return 0;
	if (m->active) {
		t->noexp = 1;
		return 0;
	}
	if (m->builtin) {
		Tok *r;
		if (m->builtin == 1) {
			r = new_tok(T_STR, t);
			r->str = xstrdup(t->file);
			r->len = (int)strlen(t->file);
			r->text = fmt("\"%s\"", t->file);
		} else {
			r = num_tok((unsigned)t->line, t);
		}
		r->space = t->space;
		unget(r);
		return 1;
	}
	if (m->fn) {
		Tok *n = raw_next();
		while (n && n->kind == T_EOF && 0) n = raw_next();
		if (!n || !is_punct(n, "(")) {
			if (n) unget(n);
			return 0;
		}
		read_args(m, t, args);
		push_frame(substitute(m, args, t), m, 0);
	} else {
		push_frame(substitute(m, NULL, t), m, 0);
	}
	if (in->frames->tok) in->frames->tok->space = t->space;
	return 1;
}

static Tok *expand_list(Tok *list) {
	Input sub, *save = in;
	Tok head, *cur = &head, *t;
	head.next = NULL;
	sub.frames = NULL;
	sub.back = NULL;
	in = &sub;
	push_frame(list, NULL, 0);
	while ((t = raw_next()) != NULL) {
		if (t->kind == T_IDENT && expand(t)) continue;
		t = copy_tok(t);
		cur->next = t;
		cur = t;
	}
	in = save;
	return head.next;
}

void pp_define(const char *def) {
	char *s = xstrdup(def), *eq = strchr(s, '=');
	Macro *m;
	char *val = "1";
	if (eq) {
		*eq = 0;
		val = eq + 1;
	}
	m = add_macro(s);
	m->body = tokenize(xstrdup(val), "<command line>");
	if (m->body->kind == T_EOF) m->body = NULL;
	else {
		Tok *t = m->body;
		while (t->next && t->next->kind != T_EOF) t = t->next;
		t->next = NULL;
	}
}

Tok *preprocess(const char *path) {
	Input main_in;
	Tok head, *cur = &head, *t;
	int len;
	char *src;
	Macro *m;
	src = read_file(path, &len);
	if (!src) fatal("cannot open '%s'", path);
	m = add_macro("__FILE__");
	m->builtin = 1;
	m = add_macro("__LINE__");
	m->builtin = 2;
	pp_define("__STDC__=1");
	pp_define("__TORTOISE__=1");
	pp_define("__tcc0__=1");
	main_in.frames = NULL;
	main_in.back = NULL;
	in = &main_in;
	include_depth = 0;
	push_frame(tokenize(src, xstrdup(path)), NULL, 1);
	head.next = NULL;
	while (1) {
		Frame *tf;
		t = raw_next();
		if (!t) break;
		tf = top_file();
		if (t->kind == T_EOF) {
			if (conds) error_at(conds->tok, "unterminated #%s", conds->tok->text);
			cur->next = t;
			break;
		}
		if (t->bol && is_punct(t, "#") && in->frames == tf && !in->back) {
			directive(t);
			continue;
		}
		if (skipping()) continue;
		if (t->kind == T_PUNCT && t->len == -1) error_at(t, "%s", t->str);
		if (t->kind == T_IDENT && expand(t)) continue;
		t = copy_tok(t);
		cur->next = t;
		cur = t;
	}
	return head.next;
}
