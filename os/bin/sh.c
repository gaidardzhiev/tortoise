/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <glob.h>
#include <fnmatch.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <plan9.h>
#include "testcore.h"

#define N_SIMPLE 1
#define N_PIPE 2
#define N_LIST 3
#define N_AND 4
#define N_OR 5
#define N_IF 6
#define N_WHILE 7
#define N_UNTIL 8
#define N_FOR 9
#define N_CASE 10
#define N_GROUP 11
#define N_SUBSHELL 12
#define N_FUNC 13

#define R_IN 1
#define R_OUT 2
#define R_APP 3
#define R_DUPIN 4
#define R_DUPOUT 5
#define R_RDWR 6

#define T_EOF 0
#define T_WORD 1
#define T_NL 2
#define T_SEMI 3
#define T_AMP 4
#define T_PIPE 5
#define T_AND 6
#define T_OR 7
#define T_DSEMI 8
#define T_LP 9
#define T_RP 10
#define T_REDIR 11

#define CTL_BREAK 1
#define CTL_CONTINUE 2
#define CTL_RETURN 3

#define F_NOFORK 1

#define MAXJOBS 32
#define MAXPROCS 16

typedef struct Redir {
	int fd, op;
	char *word;
	struct Redir *next;
} Redir;

typedef struct CaseItem {
	char **pats;
	int npats;
	struct Node *body;
	struct CaseItem *next;
} CaseItem;

typedef struct Node {
	int type;
	struct Node *a, *b, *c;
	struct Node **kids;
	int *bg;
	int nkids;
	char **words;
	int nwords;
	char **assigns;
	int nassigns;
	Redir *redirs;
	char *name;
	CaseItem *items;
	int bang;
	int has_in;
	char *text;
} Node;

typedef struct Var {
	char *name;
	char *value;
	int exported;
	struct Var *next;
} Var;

typedef struct Func {
	char *name;
	Node *body;
	struct Func *next;
} Func;

typedef struct Job {
	int used, num, pgid, nprocs, alive, stopped, notified, status;
	int pids[MAXPROCS];
	int done[MAXPROCS];
	char text[128];
} Job;

typedef struct Field {
	char *s;
	char *q;
	int len, cap, forced;
} Field;

typedef struct Fields {
	char **v;
	int n, cap;
} Fields;

static char *src;
static int pos, perr, tok, tok_fd, tok_op, tok_start;
static char *tok_word;
static int peeked;

static Var *vars;
static Func *funcs;
static char **params;
static int nparams;
static char *arg0 = "sh";
static int last_status, shell_pid, last_bg, interactive, jobctl, shell_pgid;
static int opt_e, opt_x, ctl, ctl_n, cond_depth, in_child;
static Job jobs[MAXJOBS];
static int cur_job, prev_job;

static int run(Node *n, int flags);
static int run_string(const char *s);
static void expand_word(const char *w, Fields *out, int split);
static char *expand_one(const char *w);

static void *xmalloc(unsigned n) {
	void *p = malloc(n);
	if (!p) {
		fprintf(stderr, "sh: out of memory\n");
		_exit(2);
	}
	return p;
}

static char *xstrdup(const char *s) {
	char *d = xmalloc(strlen(s) + 1);
	strcpy(d, s);
	return d;
}

static char *xstrndup(const char *s, int n) {
	char *d = xmalloc((unsigned)n + 1);
	memcpy(d, s, (unsigned)n);
	d[n] = 0;
	return d;
}

static Node *node(int type) {
	Node *n = xmalloc(sizeof(Node));
	memset(n, 0, sizeof(Node));
	n->type = type;
	return n;
}

static void push_str(char ***v, int *n, char *s) {
	char **nv = xmalloc((unsigned)(*n + 2) * sizeof(char *));
	if (*n) memcpy(nv, *v, (unsigned)*n * sizeof(char *));
	nv[(*n)++] = s;
	nv[*n] = NULL;
	if (*v) free(*v);
	*v = nv;
}

static void push_kid(Node *p, Node *k, int bg) {
	Node **nk = xmalloc((unsigned)(p->nkids + 1) * sizeof(Node *));
	int *nb = xmalloc((unsigned)(p->nkids + 1) * sizeof(int));
	if (p->nkids) {
		memcpy(nk, p->kids, (unsigned)p->nkids * sizeof(Node *));
		memcpy(nb, p->bg, (unsigned)p->nkids * sizeof(int));
		free(p->kids);
		free(p->bg);
	}
	nk[p->nkids] = k;
	nb[p->nkids] = bg;
	p->kids = nk;
	p->bg = nb;
	p->nkids++;
}

static char *getvar(const char *name) {
	Var *v;
	for (v = vars; v; v = v->next)
		if (!strcmp(v->name, name)) return v->value;
	return NULL;
}

static Var *findvar(const char *name) {
	Var *v;
	for (v = vars; v; v = v->next)
		if (!strcmp(v->name, name)) return v;
	return NULL;
}

static void setvar(const char *name, const char *value, int export) {
	Var *v = findvar(name);
	if (!v) {
		v = xmalloc(sizeof(Var));
		v->name = xstrdup(name);
		v->value = NULL;
		v->exported = 0;
		v->next = vars;
		vars = v;
	}
	if (value) {
		if (v->value) free(v->value);
		v->value = xstrdup(value);
	}
	if (export) v->exported = 1;
}

static void unsetvar(const char *name) {
	Var **pp;
	for (pp = &vars; *pp; pp = &(*pp)->next)
		if (!strcmp((*pp)->name, name)) {
			Var *v = *pp;
			*pp = v->next;
			free(v->name);
			if (v->value) free(v->value);
			free(v);
			return;
		}
}

static char **make_env(void) {
	Var *v;
	int n = 0, i = 0;
	char **e;
	for (v = vars; v; v = v->next)
		if (v->exported && v->value) n++;
	e = xmalloc((unsigned)(n + 1) * sizeof(char *));
	for (v = vars; v; v = v->next)
		if (v->exported && v->value) {
			e[i] = xmalloc(strlen(v->name) + strlen(v->value) + 2);
			sprintf(e[i], "%s=%s", v->name, v->value);
			i++;
		}
	e[i] = NULL;
	return e;
}

static int valid_name(const char *s, int n) {
	int i;
	if (n <= 0 || !(s[0] == '_' || (s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return 0;
	for (i = 1; i < n; i++)
		if (!(s[i] == '_' || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9'))) return 0;
	return 1;
}

static Func *findfunc(const char *name) {
	Func *f;
	for (f = funcs; f; f = f->next)
		if (!strcmp(f->name, name)) return f;
	return NULL;
}

static int is_meta(int c) {
	return c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' || c == '<' || c == '>' || c == '(' || c == ')' || c == 0;
}

static const char *skip_subst(const char *s) {
	int depth = 1;
	while (*s && depth > 0) {
		if (*s == '\\' && s[1]) s += 2;
		else if (*s == '\'') {
			s++;
			while (*s && *s != '\'') s++;
			if (*s) s++;
		} else if (*s == '"') {
			s++;
			while (*s && *s != '"') {
				if (*s == '\\' && s[1]) s++;
				s++;
			}
			if (*s) s++;
		} else {
			if (*s == '(') depth++;
			else if (*s == ')') depth--;
			s++;
		}
	}
	return depth ? NULL : s;
}

static int lex(void) {
	int c, start;
	tok_word = NULL;
	for (;;) {
		while (src[pos] == ' ' || src[pos] == '\t') pos++;
		if (src[pos] == '\\' && src[pos + 1] == '\n') {
			pos += 2;
			continue;
		}
		if (src[pos] == '#') {
			while (src[pos] && src[pos] != '\n') pos++;
		}
		break;
	}
	tok_start = pos;
	c = src[pos];
	if (!c) return T_EOF;
	if (c == '\n') {
		pos++;
		return T_NL;
	}
	if (c == ';') {
		pos++;
		if (src[pos] == ';') {
			pos++;
			return T_DSEMI;
		}
		return T_SEMI;
	}
	if (c == '&') {
		pos++;
		if (src[pos] == '&') {
			pos++;
			return T_AND;
		}
		return T_AMP;
	}
	if (c == '|') {
		pos++;
		if (src[pos] == '|') {
			pos++;
			return T_OR;
		}
		return T_PIPE;
	}
	if (c == '(') {
		pos++;
		return T_LP;
	}
	if (c == ')') {
		pos++;
		return T_RP;
	}
	start = pos;
	while (src[pos] >= '0' && src[pos] <= '9') pos++;
	if ((src[pos] == '<' || src[pos] == '>') && (pos > start || 1)) {
		tok_fd = pos > start ? atoi(src + start) : -1;
		if (src[pos] == '<') {
			pos++;
			if (src[pos] == '&') {
				pos++;
				tok_op = R_DUPIN;
			} else if (src[pos] == '>') {
				pos++;
				tok_op = R_RDWR;
			} else {
				tok_op = R_IN;
			}
		} else {
			pos++;
			if (src[pos] == '>') {
				pos++;
				tok_op = R_APP;
			} else if (src[pos] == '&') {
				pos++;
				tok_op = R_DUPOUT;
			} else {
				if (src[pos] == '|') pos++;
				tok_op = R_OUT;
			}
		}
		return T_REDIR;
	}
	pos = start;
	while (!is_meta(src[pos])) {
		c = src[pos];
		if (c == '\\') {
			if (!src[pos + 1]) {
				perr = 2;
				return T_EOF;
			}
			pos += 2;
		} else if (c == '\'') {
			pos++;
			while (src[pos] && src[pos] != '\'') pos++;
			if (!src[pos]) {
				perr = 2;
				return T_EOF;
			}
			pos++;
		} else if (c == '"') {
			pos++;
			while (src[pos] && src[pos] != '"') {
				if (src[pos] == '\\' && src[pos + 1]) pos++;
				else if (src[pos] == '$' && src[pos + 1] == '(') {
					const char *e = skip_subst(src + pos + 2);
					if (!e) {
						perr = 2;
						return T_EOF;
					}
					pos = (int)(e - src) - 1;
				}
				pos++;
			}
			if (!src[pos]) {
				perr = 2;
				return T_EOF;
			}
			pos++;
		} else if (c == '$' && src[pos + 1] == '(') {
			const char *e = skip_subst(src + pos + 2);
			if (!e) {
				perr = 2;
				return T_EOF;
			}
			pos = (int)(e - src);
		} else if (c == '$' && src[pos + 1] == '{') {
			while (src[pos] && src[pos] != '}') pos++;
			if (!src[pos]) {
				perr = 2;
				return T_EOF;
			}
			pos++;
		} else {
			pos++;
		}
	}
	tok_word = xstrndup(src + start, pos - start);
	return T_WORD;
}

static int peek(void) {
	if (!peeked) {
		tok = lex();
		peeked = 1;
	}
	return tok;
}

static int next(void) {
	int t = peek();
	peeked = 0;
	return t;
}

static int is_kw(const char *w) {
	return peek() == T_WORD && !strcmp(tok_word, w);
}

static void syntax(const char *what) {
	if (perr) return;
	if (peek() == T_EOF) {
		perr = 2;
		return;
	}
	perr = 1;
	fprintf(stderr, "sh: syntax error near %s\n", what);
}

static void linebreak(void) {
	while (peek() == T_NL) next();
}

static int list_end(void) {
	int t = peek();
	if (t == T_EOF || t == T_RP || t == T_DSEMI) return 1;
	if (t == T_WORD)
		return !strcmp(tok_word, "then") || !strcmp(tok_word, "else") || !strcmp(tok_word, "elif") || !strcmp(tok_word, "fi") || !strcmp(tok_word, "do") || !strcmp(tok_word, "done") || !strcmp(tok_word, "esac") || !strcmp(tok_word, "}");
	return 0;
}

static Node *parse_list(void);
static Node *parse_command(void);

static int expect_kw(const char *w) {
	if (perr) return 0;
	if (!is_kw(w)) {
		syntax(peek() == T_WORD ? tok_word : peek() == T_EOF ? "end of file" : "operator");
		return 0;
	}
	next();
	return 1;
}

static Redir *parse_redir(void) {
	Redir *r = xmalloc(sizeof(Redir));
	r->op = tok_op;
	r->fd = tok_fd >= 0 ? tok_fd : (tok_op == R_IN || tok_op == R_DUPIN || tok_op == R_RDWR) ? 0 : 1;
	r->next = NULL;
	next();
	if (peek() != T_WORD) {
		syntax("redirection");
		return NULL;
	}
	r->word = tok_word;
	next();
	return r;
}

static void add_redirs(Node *n) {
	Redir **pp = &n->redirs;
	while (*pp) pp = &(*pp)->next;
	while (!perr && peek() == T_REDIR) {
		Redir *r = parse_redir();
		if (!r) return;
		*pp = r;
		pp = &r->next;
	}
}

static Node *parse_compound_list(void) {
	Node *n;
	linebreak();
	n = parse_list();
	return n;
}

static Node *parse_if(void) {
	Node *n = node(N_IF);
	next();
	n->a = parse_compound_list();
	if (!expect_kw("then")) return NULL;
	n->b = parse_compound_list();
	if (perr) return NULL;
	if (is_kw("elif")) {
		n->c = parse_if();
		return perr ? NULL : n;
	}
	if (is_kw("else")) {
		next();
		n->c = parse_compound_list();
	}
	if (!expect_kw("fi")) return NULL;
	return n;
}

static Node *parse_loop(int type) {
	Node *n = node(type);
	next();
	n->a = parse_compound_list();
	if (!expect_kw("do")) return NULL;
	n->b = parse_compound_list();
	if (!expect_kw("done")) return NULL;
	return n;
}

static Node *parse_for(void) {
	Node *n = node(N_FOR);
	next();
	if (peek() != T_WORD || !valid_name(tok_word, (int)strlen(tok_word))) {
		syntax("for");
		return NULL;
	}
	n->name = tok_word;
	next();
	linebreak();
	if (is_kw("in")) {
		next();
		n->has_in = 1;
		while (peek() == T_WORD) {
			push_str(&n->words, &n->nwords, tok_word);
			next();
		}
		if (peek() == T_SEMI || peek() == T_NL) next();
		else {
			syntax("for");
			return NULL;
		}
	} else if (peek() == T_SEMI) {
		next();
	}
	linebreak();
	if (!expect_kw("do")) return NULL;
	n->b = parse_compound_list();
	if (!expect_kw("done")) return NULL;
	return n;
}

static Node *parse_case(void) {
	Node *n = node(N_CASE);
	CaseItem **ip = &n->items;
	next();
	if (peek() != T_WORD) {
		syntax("case");
		return NULL;
	}
	push_str(&n->words, &n->nwords, tok_word);
	next();
	linebreak();
	if (!expect_kw("in")) return NULL;
	linebreak();
	while (!perr && !is_kw("esac")) {
		CaseItem *it = xmalloc(sizeof(CaseItem));
		memset(it, 0, sizeof(CaseItem));
		if (peek() == T_LP) next();
		for (;;) {
			if (peek() != T_WORD) {
				syntax("case pattern");
				return NULL;
			}
			push_str(&it->pats, &it->npats, tok_word);
			next();
			if (peek() == T_PIPE) {
				next();
				continue;
			}
			break;
		}
		if (peek() != T_RP) {
			syntax("case pattern");
			return NULL;
		}
		next();
		it->body = parse_compound_list();
		if (perr) return NULL;
		*ip = it;
		ip = &it->next;
		if (peek() == T_DSEMI) {
			next();
			linebreak();
		} else if (!is_kw("esac")) {
			syntax("case");
			return NULL;
		}
	}
	if (!expect_kw("esac")) return NULL;
	return n;
}

static Node *parse_simple(void) {
	Node *n = node(N_SIMPLE);
	Redir **rp = &n->redirs;
	for (;;) {
		int t = peek();
		if (t == T_REDIR) {
			Redir *r = parse_redir();
			if (!r) return NULL;
			*rp = r;
			rp = &r->next;
		} else if (t == T_WORD) {
			char *eq = strchr(tok_word, '=');
			if (n->nwords == 0 && eq && valid_name(tok_word, (int)(eq - tok_word))) push_str(&n->assigns, &n->nassigns, tok_word);
			else push_str(&n->words, &n->nwords, tok_word);
			next();
		} else {
			break;
		}
	}
	if (n->nwords == 0 && n->nassigns == 0 && !n->redirs) {
		syntax(peek() == T_EOF ? "end of file" : "operator");
		return NULL;
	}
	return n;
}

static Node *parse_command(void) {
	Node *n = NULL;
	if (perr) return NULL;
	if (peek() == T_WORD) {
		if (!strcmp(tok_word, "if")) n = parse_if();
		else if (!strcmp(tok_word, "while")) n = parse_loop(N_WHILE);
		else if (!strcmp(tok_word, "until")) n = parse_loop(N_UNTIL);
		else if (!strcmp(tok_word, "for")) n = parse_for();
		else if (!strcmp(tok_word, "case")) n = parse_case();
		else if (!strcmp(tok_word, "{")) {
			next();
			n = node(N_GROUP);
			n->a = parse_compound_list();
			if (!expect_kw("}")) return NULL;
		} else {
			int save = pos;
			char *w = tok_word;
			next();
			if (peek() == T_LP && valid_name(w, (int)strlen(w))) {
				next();
				if (peek() != T_RP) {
					syntax("(");
					return NULL;
				}
				next();
				linebreak();
				n = node(N_FUNC);
				n->name = w;
				n->a = parse_command();
				if (!n->a) return NULL;
				return n;
			}
			pos = save;
			tok = T_WORD;
			tok_word = w;
			peeked = 1;
			return parse_simple();
		}
		if (!n) return NULL;
		add_redirs(n);
		return perr ? NULL : n;
	}
	if (peek() == T_LP) {
		next();
		n = node(N_SUBSHELL);
		n->a = parse_compound_list();
		if (perr) return NULL;
		if (peek() != T_RP) {
			syntax(")");
			return NULL;
		}
		next();
		add_redirs(n);
		return perr ? NULL : n;
	}
	return parse_simple();
}

static char *trim_text(int a, int b) {
	while (a < b && (src[a] == ' ' || src[a] == '\t')) a++;
	while (b > a && (src[b - 1] == ' ' || src[b - 1] == '\t' || src[b - 1] == '\n')) b--;
	return xstrndup(src + a, b - a);
}

static Node *parse_pipeline(void) {
	Node *n = node(N_PIPE), *c;
	int start;
	peek();
	start = tok_start;
	if (is_kw("!")) {
		n->bang = 1;
		next();
	}
	for (;;) {
		c = parse_command();
		if (!c) return NULL;
		push_kid(n, c, 0);
		if (peek() != T_PIPE) break;
		next();
		linebreak();
	}
	peek();
	n->text = trim_text(start, peeked ? tok_start : pos);
	if (n->nkids == 1 && !n->kids[0]->text) n->kids[0]->text = n->text;
	return n;
}

static Node *parse_and_or(void) {
	Node *l = parse_pipeline();
	while (l && !perr && (peek() == T_AND || peek() == T_OR)) {
		Node *n = node(next() == T_AND ? N_AND : N_OR);
		linebreak();
		n->a = l;
		n->b = parse_pipeline();
		if (!n->b) return NULL;
		l = n;
	}
	return l;
}

static Node *parse_list(void) {
	Node *n = node(N_LIST);
	linebreak();
	while (!perr && !list_end()) {
		Node *item = parse_and_or();
		int t;
		if (!item) return NULL;
		t = peek();
		if (t == T_AMP) {
			next();
			push_kid(n, item, 1);
		} else {
			push_kid(n, item, 0);
			if (t == T_SEMI || t == T_NL) next();
			else break;
		}
		linebreak();
	}
	return perr ? NULL : n;
}

static Node *parse_program(const char *s) {
	Node *n;
	src = (char *)s;
	pos = 0;
	perr = 0;
	peeked = 0;
	n = parse_list();
	if (!perr && peek() != T_EOF) {
		syntax(peek() == T_WORD ? tok_word : "operator");
		n = NULL;
	}
	return perr ? NULL : n;
}

static void f_init(Field *f) {
	f->cap = 32;
	f->s = xmalloc(32);
	f->q = xmalloc(32);
	f->len = 0;
	f->forced = 0;
}

static void f_add(Field *f, int c, int q) {
	if (f->len + 2 > f->cap) {
		char *s = xmalloc((unsigned)f->cap * 2), *qq = xmalloc((unsigned)f->cap * 2);
		memcpy(s, f->s, (unsigned)f->len);
		memcpy(qq, f->q, (unsigned)f->len);
		free(f->s);
		free(f->q);
		f->s = s;
		f->q = qq;
		f->cap *= 2;
	}
	f->s[f->len] = (char)c;
	f->q[f->len] = (char)q;
	f->len++;
}

static void fs_add(Fields *fs, char *s) {
	if (fs->n + 2 > fs->cap) {
		char **v = xmalloc((unsigned)(fs->cap * 2 + 8) * sizeof(char *));
		if (fs->n) memcpy(v, fs->v, (unsigned)fs->n * sizeof(char *));
		if (fs->v) free(fs->v);
		fs->v = v;
		fs->cap = fs->cap * 2 + 8;
	}
	fs->v[fs->n++] = s;
	fs->v[fs->n] = NULL;
}

static int has_glob(Field *f) {
	int i;
	for (i = 0; i < f->len; i++)
		if (!(f->q[i] & 1) && (f->s[i] == '*' || f->s[i] == '?' || f->s[i] == '[')) return 1;
	return 0;
}

static void finish(Field *f, Fields *out, int split) {
	char *plain;
	if (f->len == 0 && !f->forced) return;
	plain = xstrndup(f->s, f->len);
	if (split && has_glob(f)) {
		glob_t g;
		char *pat = xmalloc((unsigned)f->len * 2 + 1);
		int i, k = 0;
		for (i = 0; i < f->len; i++) {
			if ((f->q[i] & 1) && strchr("*?[]\\", f->s[i])) pat[k++] = '\\';
			pat[k++] = f->s[i];
		}
		pat[k] = 0;
		if (glob(pat, 0, NULL, &g) == 0) {
			unsigned j;
			for (j = 0; j < g.gl_pathc; j++) fs_add(out, xstrdup(g.gl_pathv[j]));
			globfree(&g);
			free(pat);
			free(plain);
			f->len = 0;
			f->forced = 0;
			return;
		}
		free(pat);
	}
	fs_add(out, plain);
	f->len = 0;
	f->forced = 0;
}

static int is_ifs(int c) {
	char *ifs = getvar("IFS");
	if (!ifs) ifs = " \t\n";
	return c && strchr(ifs, c) != NULL;
}

static void add_value(Field *f, Fields *out, const char *v, int quoted, int split) {
	for (; *v; v++) {
		if (!quoted && split && is_ifs((unsigned char)*v)) {
			finish(f, out, split);
			continue;
		}
		f_add(f, (unsigned char)*v, quoted ? 1 : 0);
	}
}

static char *command_subst(const char *cmd) {
	int p[2], pid, n, len = 0, cap = 256, st;
	char *buf = xmalloc((unsigned)cap), tmp[256];
	fflush(stdout);
	if (pipe(p) < 0) {
		buf[0] = 0;
		return buf;
	}
	pid = fork();
	if (pid == 0) {
		close(p[0]);
		dup2(p[1], 1);
		close(p[1]);
		in_child = 1;
		jobctl = 0;
		interactive = 0;
		_exit(run_string(cmd));
	}
	close(p[1]);
	while ((n = read(p[0], tmp, sizeof(tmp))) > 0) {
		if (len + n + 1 > cap) {
			char *nb = xmalloc((unsigned)(cap * 2 + n));
			memcpy(nb, buf, (unsigned)len);
			free(buf);
			buf = nb;
			cap = cap * 2 + n;
		}
		memcpy(buf + len, tmp, (unsigned)n);
		len += n;
	}
	close(p[0]);
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) ;
	last_status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
	while (len > 0 && buf[len - 1] == '\n') len--;
	buf[len] = 0;
	return buf;
}

static const char *param(const char *p, Field *f, Fields *out, int quoted, int split) {
	char name[64], num[16];
	int n = 0, braced = 0;
	char *v = NULL;
	if (*p == '(') {
		const char *e = skip_subst(p + 1);
		char *cmd, *res;
		if (!e) e = p + strlen(p);
		cmd = xstrndup(p + 1, (int)(e - p - 2));
		res = command_subst(cmd);
		add_value(f, out, res, quoted, split);
		free(cmd);
		free(res);
		return e;
	}
	if (*p == '{') {
		braced = 1;
		p++;
	}
	if (*p == '?' || *p == '$' || *p == '!' || *p == '#' || *p == '@' || *p == '*' || *p == '-' || (*p >= '0' && *p <= '9')) {
		name[n++] = *p++;
		if (braced)
			while (*p >= '0' && *p <= '9' && n < 62) name[n++] = *p++;
	} else {
		while ((*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (n > 0 && *p >= '0' && *p <= '9')) && n < 62) name[n++] = *p++;
	}
	name[n] = 0;
	if (braced) {
		while (*p && *p != '}') p++;
		if (*p) p++;
	}
	if (n == 0) {
		f_add(f, '$', quoted);
		return p;
	}
	if (!strcmp(name, "@") || !strcmp(name, "*")) {
		int i;
		if (quoted && name[0] == '@') {
			for (i = 0; i < nparams; i++) {
				if (i) finish(f, out, 0);
				add_value(f, out, params[i], 1, 0);
				f->forced = 1;
			}
			return p;
		}
		for (i = 0; i < nparams; i++) {
			if (i) {
				if (quoted) f_add(f, ' ', 1);
				else finish(f, out, split);
			}
			add_value(f, out, params[i], quoted, split);
		}
		return p;
	}
	if (!strcmp(name, "?")) sprintf(num, "%d", last_status), v = num;
	else if (!strcmp(name, "$")) sprintf(num, "%d", shell_pid), v = num;
	else if (!strcmp(name, "!")) {
		if (last_bg) sprintf(num, "%d", last_bg), v = num;
	} else if (!strcmp(name, "#")) sprintf(num, "%d", nparams), v = num;
	else if (!strcmp(name, "-")) {
		num[0] = 0;
		if (opt_e) strcat(num, "e");
		if (opt_x) strcat(num, "x");
		if (interactive) strcat(num, "i");
		v = num;
	} else if (name[0] >= '0' && name[0] <= '9') {
		int k = atoi(name);
		v = k == 0 ? arg0 : k <= nparams ? params[k - 1] : NULL;
	} else {
		v = getvar(name);
	}
	if (v) add_value(f, out, v, quoted, split);
	return p;
}

static void expand_word(const char *w, Fields *out, int split) {
	Field f;
	const char *p = w;
	f_init(&f);
	if (*p == '~' && (p[1] == '/' || p[1] == 0)) {
		char *home = getvar("HOME");
		add_value(&f, out, home ? home : "/", 1, 0);
		p++;
	}
	while (*p) {
		if (*p == '\\') {
			if (p[1]) {
				f_add(&f, (unsigned char)p[1], 1);
				p += 2;
			} else {
				p++;
			}
		} else if (*p == '\'') {
			p++;
			f.forced = 1;
			while (*p && *p != '\'') f_add(&f, (unsigned char)*p++, 1);
			if (*p) p++;
		} else if (*p == '"') {
			p++;
			f.forced = 1;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1] && strchr("$`\"\\\n", p[1])) {
					if (p[1] != '\n') f_add(&f, (unsigned char)p[1], 1);
					p += 2;
				} else if (*p == '$') {
					p = param(p + 1, &f, out, 1, split);
				} else {
					f_add(&f, (unsigned char)*p++, 1);
				}
			}
			if (*p) p++;
		} else if (*p == '$') {
			p = param(p + 1, &f, out, 0, split);
		} else {
			f_add(&f, (unsigned char)*p++, 0);
		}
	}
	finish(&f, out, split);
	free(f.s);
	free(f.q);
}

static char *expand_one(const char *w) {
	Fields fs;
	char *r;
	int i, l = 0;
	fs.v = NULL;
	fs.n = fs.cap = 0;
	expand_word(w, &fs, 0);
	for (i = 0; i < fs.n; i++) l += (int)strlen(fs.v[i]) + 1;
	r = xmalloc((unsigned)l + 1);
	r[0] = 0;
	for (i = 0; i < fs.n; i++) {
		if (i) strcat(r, " ");
		strcat(r, fs.v[i]);
		free(fs.v[i]);
	}
	if (fs.v) free(fs.v);
	return r;
}

static char *expand_pattern(const char *w) {
	Field f;
	Fields out;
	const char *p = w;
	char *pat;
	int i, k = 0;
	f_init(&f);
	out.v = NULL;
	out.n = out.cap = 0;
	while (*p) {
		if (*p == '\\' && p[1]) {
			f_add(&f, (unsigned char)p[1], 1);
			p += 2;
		} else if (*p == '\'') {
			p++;
			while (*p && *p != '\'') f_add(&f, (unsigned char)*p++, 1);
			if (*p) p++;
		} else if (*p == '"') {
			p++;
			while (*p && *p != '"') {
				if (*p == '\\' && p[1] && strchr("$`\"\\", p[1])) {
					f_add(&f, (unsigned char)p[1], 1);
					p += 2;
				} else if (*p == '$') {
					p = param(p + 1, &f, &out, 1, 0);
				} else {
					f_add(&f, (unsigned char)*p++, 1);
				}
			}
			if (*p) p++;
		} else if (*p == '$') {
			p = param(p + 1, &f, &out, 1, 0);
		} else {
			f_add(&f, (unsigned char)*p++, 0);
		}
	}
	pat = xmalloc((unsigned)f.len * 2 + 1);
	for (i = 0; i < f.len; i++) {
		if ((f.q[i] & 1) && strchr("*?[]\\", f.s[i])) pat[k++] = '\\';
		pat[k++] = f.s[i];
	}
	pat[k] = 0;
	free(f.s);
	free(f.q);
	return pat;
}

static void expand_words(char **w, int n, Fields *out) {
	int i;
	out->v = NULL;
	out->n = out->cap = 0;
	for (i = 0; i < n; i++) expand_word(w[i], out, 1);
	if (!out->v) fs_add(out, NULL), out->n = 0;
}

static void free_fields(Fields *fs) {
	int i;
	for (i = 0; i < fs->n; i++) free(fs->v[i]);
	if (fs->v) free(fs->v);
}

#define SAVEMAX 16

typedef struct Saved {
	int fd[SAVEMAX], copy[SAVEMAX], n;
} Saved;

static int apply_redirs(Redir *r, Saved *sv) {
	for (; r; r = r->next) {
		char *w = expand_one(r->word);
		int fd = -1, flags = 0;
		if (sv && sv->n < SAVEMAX) {
			sv->fd[sv->n] = r->fd;
			sv->copy[sv->n] = fcntl(r->fd, F_DUPFD, 20);
			if (sv->copy[sv->n] >= 0) fcntl(sv->copy[sv->n], F_SETFD, FD_CLOEXEC);
			sv->n++;
		}
		if (r->op == R_DUPIN || r->op == R_DUPOUT) {
			if (!strcmp(w, "-")) {
				close(r->fd);
				free(w);
				continue;
			}
			fd = atoi(w);
			if (fcntl(fd, F_GETFD) < 0) {
				fprintf(stderr, "sh: %s: bad file descriptor\n", w);
				free(w);
				return -1;
			}
			if (fd != r->fd) dup2(fd, r->fd);
			free(w);
			continue;
		}
		if (r->op == R_IN) flags = O_RDONLY;
		else if (r->op == R_OUT) flags = O_WRONLY | O_CREAT | O_TRUNC;
		else if (r->op == R_APP) flags = O_WRONLY | O_CREAT | O_APPEND;
		else flags = O_RDWR | O_CREAT;
		fd = open(w, flags, 0666);
		if (fd < 0) {
			fprintf(stderr, "sh: cannot open %s\n", w);
			free(w);
			return -1;
		}
		if (fd != r->fd) {
			dup2(fd, r->fd);
			close(fd);
		}
		free(w);
	}
	return 0;
}

static void restore(Saved *sv) {
	while (sv->n > 0) {
		sv->n--;
		if (sv->copy[sv->n] >= 0) {
			dup2(sv->copy[sv->n], sv->fd[sv->n]);
			close(sv->copy[sv->n]);
		} else {
			close(sv->fd[sv->n]);
		}
	}
}

static const char *signame(int s) {
	switch (s) {
	case SIGHUP: return "Hangup";
	case SIGINT: return "Interrupt";
	case SIGQUIT: return "Quit";
	case SIGILL: return "Illegal instruction";
	case SIGTRAP: return "Trace trap";
	case SIGABRT: return "Aborted";
	case SIGBUS: return "Bus error";
	case SIGFPE: return "Floating point exception";
	case SIGKILL: return "Killed";
	case SIGUSR1: return "User signal 1";
	case SIGSEGV: return "Segmentation fault";
	case SIGUSR2: return "User signal 2";
	case SIGPIPE: return "Broken pipe";
	case SIGALRM: return "Alarm clock";
	case SIGTERM: return "Terminated";
	}
	return "Signal";
}

static int code_of(int st) {
	if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
	if (WIFSTOPPED(st)) return 128 + WSTOPSIG(st);
	return WEXITSTATUS(st);
}

static char mark(Job *j) {
	int i = (int)(j - jobs);
	return i == cur_job ? '+' : i == prev_job ? '-' : ' ';
}

static void set_current(int i) {
	if (cur_job != i) {
		prev_job = cur_job;
		cur_job = i;
	}
}

static void drop_job(int i) {
	jobs[i].used = 0;
	if (cur_job == i) {
		int k, best = -1;
		cur_job = prev_job;
		prev_job = -1;
		for (k = MAXJOBS - 1; k >= 0; k--)
			if (jobs[k].used && k != cur_job && (best < 0 || jobs[k].num > jobs[best].num)) best = k;
		prev_job = best;
		if (cur_job >= 0 && !jobs[cur_job].used) cur_job = best, prev_job = -1;
	} else if (prev_job == i) {
		prev_job = -1;
	}
}

static int new_job(int pgid, int *pids, int n, const char *text) {
	int i, num = 1, k;
	for (;;) {
		int clash = 0;
		for (k = 0; k < MAXJOBS; k++)
			if (jobs[k].used && jobs[k].num == num) clash = 1;
		if (!clash) break;
		num++;
	}
	for (i = 0; i < MAXJOBS; i++)
		if (!jobs[i].used) break;
	if (i == MAXJOBS) return -1;
	memset(&jobs[i], 0, sizeof(Job));
	jobs[i].used = 1;
	jobs[i].num = num;
	jobs[i].pgid = pgid;
	jobs[i].nprocs = n;
	jobs[i].alive = n;
	for (k = 0; k < n && k < MAXPROCS; k++) jobs[i].pids[k] = pids[k];
	strncpy(jobs[i].text, text ? text : "", sizeof(jobs[i].text) - 1);
	return i;
}

static int record(int pid, int st) {
	int i, k;
	for (i = 0; i < MAXJOBS; i++) {
		if (!jobs[i].used) continue;
		for (k = 0; k < jobs[i].nprocs; k++)
			if (jobs[i].pids[k] == pid) {
				if (WIFSTOPPED(st)) {
					jobs[i].stopped = 1;
					jobs[i].notified = 0;
				} else if (WIFCONTINUED(st)) {
					jobs[i].stopped = 0;
				} else if (!jobs[i].done[k]) {
					jobs[i].done[k] = 1;
					jobs[i].stopped = 0;
					jobs[i].alive--;
					if (k == jobs[i].nprocs - 1) jobs[i].status = st;
				}
				return i;
			}
	}
	return -1;
}

static void report_job(int i) {
	Job *j = &jobs[i];
	int st = j->status;
	if (j->stopped) printf("[%d]%c Stopped %s\n", j->num, mark(j), j->text);
	else if (WIFSIGNALED(st)) printf("[%d]%c %s %s\n", j->num, mark(j), signame(WTERMSIG(st)), j->text);
	else if (WEXITSTATUS(st)) printf("[%d]%c Exit %d %s\n", j->num, mark(j), WEXITSTATUS(st), j->text);
	else printf("[%d]%c Done %s\n", j->num, mark(j), j->text);
}

static void report_jobs(void) {
	int pid, st, i;
	while ((pid = waitpid(-1, &st, WNOHANG | WUNTRACED)) > 0) record(pid, st);
	for (i = 0; i < MAXJOBS; i++) {
		if (!jobs[i].used) continue;
		if (jobs[i].alive == 0) {
			if (interactive) report_job(i);
			drop_job(i);
		} else if (jobs[i].stopped && !jobs[i].notified) {
			if (interactive) report_job(i);
			jobs[i].notified = 1;
		}
	}
	fflush(stdout);
}

static int wait_job(int i, int fg) {
	int st, pid, r;
	while (jobs[i].alive > 0 && !jobs[i].stopped) {
		pid = waitpid(-1, &st, WUNTRACED);
		if (pid < 0) {
			if (errno == EINTR) continue;
			break;
		}
		record(pid, st);
	}
	if (fg && jobctl) tcsetpgrp(0, shell_pgid);
	if (jobs[i].stopped) {
		set_current(i);
		jobs[i].notified = 1;
		if (interactive) {
			printf("\n");
			report_job(i);
		}
		return 128 + SIGTSTP;
	}
	r = code_of(jobs[i].status);
	if (fg && interactive && r >= 128) printf("sh: pid %d terminated, status %d\n", jobs[i].pids[jobs[i].nprocs - 1], r);
	drop_job(i);
	return r;
}

static void child_signals(int bg) {
	if (!interactive) return;
	if (jobctl || !bg) {
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
	}
	signal(SIGTSTP, SIG_DFL);
	signal(SIGTTIN, SIG_DFL);
	signal(SIGTTOU, SIG_DFL);
}

static void exec_external(char **argv, Node *cmd) {
	char **env, path[256], *p, *pathvar;
	int i, err = ENOENT;
	for (i = 0; i < cmd->nassigns; i++) {
		char *eq = strchr(cmd->assigns[i], '='), *name = xstrndup(cmd->assigns[i], (int)(eq - cmd->assigns[i])), *val = expand_one(eq + 1);
		setvar(name, val, 1);
	}
	env = make_env();
	if (strchr(argv[0], '/')) {
		execve(argv[0], argv, env);
		err = errno;
		if (err == ENOEXEC) {
			char *sargv[66];
			sargv[0] = "sh";
			for (i = 0; argv[i] && i < 64; i++) sargv[i + 1] = argv[i];
			sargv[i + 1] = NULL;
			execve("/bin/sh", sargv, env);
		}
	} else {
		pathvar = getvar("PATH");
		if (!pathvar) pathvar = "/bin";
		p = pathvar;
		for (;;) {
			int k = 0;
			while (*p && *p != ':' && k < 200) path[k++] = *p++;
			if (k == 0) path[k++] = '.';
			path[k++] = '/';
			strncpy(path + k, argv[0], sizeof(path) - (unsigned)k - 1);
			path[sizeof(path) - 1] = 0;
			execve(path, argv, env);
			if (errno == EACCES) err = EACCES;
			else if (errno == ENOEXEC) {
				char *sargv[66];
				sargv[0] = "sh";
				sargv[1] = path;
				for (i = 1; argv[i] && i < 63; i++) sargv[i + 1] = argv[i];
				sargv[i + 1] = NULL;
				execve("/bin/sh", sargv, env);
			}
			if (!*p) break;
			p++;
		}
	}
	if (err == EACCES || err == EISDIR) {
		fprintf(stderr, "sh: %s: permission denied\n", argv[0]);
		_exit(126);
	}
	fprintf(stderr, "sh: %s: not found\n", argv[0]);
	_exit(127);
}

static int run_builtin(char **argv, int argc, int *handled);

static int is_special(const char *n) {
	static const char *b[] = {"cd", "pwd", "exit", "export", "unset", "set", "shift", "read", "true", "false", ":", "test", "[", "exec", "wait", "jobs", "fg", "bg", "kill", "umask", "rfork", "source", ".", "eval", "break", "continue", "return", NULL};
	int i;
	for (i = 0; b[i]; i++)
		if (!strcmp(b[i], n)) return 1;
	return 0;
}

static void trace(char **argv, int n) {
	int i;
	if (!opt_x) return;
	fprintf(stderr, "+");
	for (i = 0; i < n; i++) fprintf(stderr, " %s", argv[i]);
	fprintf(stderr, "\n");
}

static int run_simple(Node *n, int flags) {
	Fields fs;
	Saved sv;
	int i, r = 0, pid;
	Func *fn;
	expand_words(n->words, n->nwords, &fs);
	sv.n = 0;
	if (fs.n == 0) {
		last_status = 0;
		for (i = 0; i < n->nassigns; i++) {
			char *eq = strchr(n->assigns[i], '='), *name = xstrndup(n->assigns[i], (int)(eq - n->assigns[i])), *val = expand_one(eq + 1);
			Var *v = findvar(name);
			setvar(name, val, v ? v->exported : 0);
			free(name);
			free(val);
		}
		r = last_status;
		if (apply_redirs(n->redirs, &sv) < 0) r = 1;
		restore(&sv);
		free_fields(&fs);
		return r;
	}
	trace(fs.v, fs.n);
	fn = findfunc(fs.v[0]);
	if (fn || is_special(fs.v[0])) {
		for (i = 0; i < n->nassigns; i++) {
			char *eq = strchr(n->assigns[i], '='), *name = xstrndup(n->assigns[i], (int)(eq - n->assigns[i])), *val = expand_one(eq + 1);
			Var *v = findvar(name);
			setvar(name, val, v ? v->exported : 0);
			free(name);
			free(val);
		}
		if (strcmp(fs.v[0], "exec") == 0) {
			if (apply_redirs(n->redirs, NULL) < 0) {
				free_fields(&fs);
				return 1;
			}
			if (fs.n > 1) {
				fflush(stdout);
				exec_external(fs.v + 1, n);
			}
			free_fields(&fs);
			return 0;
		}
		if (apply_redirs(n->redirs, &sv) < 0) {
			restore(&sv);
			free_fields(&fs);
			return 1;
		}
		if (fn) {
			char **op = params, *oa = arg0;
			int on = nparams;
			params = fs.v + 1;
			nparams = fs.n - 1;
			r = run(fn->body, 0);
			if (ctl == CTL_RETURN) ctl = 0;
			r = last_status;
			params = op;
			nparams = on;
			arg0 = oa;
		} else {
			int handled;
			r = run_builtin(fs.v, fs.n, &handled);
		}
		fflush(stdout);
		restore(&sv);
		free_fields(&fs);
		return r;
	}
	fflush(stdout);
	if (flags & F_NOFORK) {
		if (apply_redirs(n->redirs, NULL) < 0) _exit(1);
		exec_external(fs.v, n);
	}
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "sh: fork failed\n");
		free_fields(&fs);
		return 1;
	}
	if (pid == 0) {
		in_child = 1;
		if (jobctl) {
			setpgid(0, 0);
			tcsetpgrp(0, getpid());
		}
		child_signals(0);
		if (apply_redirs(n->redirs, NULL) < 0) _exit(1);
		exec_external(fs.v, n);
	}
	if (jobctl) {
		setpgid(pid, pid);
		tcsetpgrp(0, pid);
	}
	free_fields(&fs);
	{
		int j = new_job(jobctl ? pid : 0, &pid, 1, n->text);
		if (j < 0) {
			int st;
			waitpid(pid, &st, 0);
			return code_of(st);
		}
		return wait_job(j, 1);
	}
}

static int run_pipeline(Node *p, int bg) {
	int i, prev = -1, pgid = 0, pids[MAXPROCS], np = 0, r;
	if (p->nkids == 1 && !bg) {
		r = run(p->kids[0], 0);
		return p->bang ? !r : r;
	}
	fflush(stdout);
	for (i = 0; i < p->nkids; i++) {
		int fds[2], pid;
		fds[0] = fds[1] = -1;
		if (i < p->nkids - 1 && pipe(fds) < 0) {
			fprintf(stderr, "sh: pipe failed\n");
			break;
		}
		pid = fork();
		if (pid == 0) {
			in_child = 1;
			if (jobctl) {
				setpgid(0, pgid ? pgid : getpid());
				if (!bg) tcsetpgrp(0, pgid ? pgid : getpid());
			}
			child_signals(bg);
			if (bg && !jobctl && interactive) {
				signal(SIGINT, SIG_IGN);
				signal(SIGQUIT, SIG_IGN);
			}
			if (prev >= 0) {
				dup2(prev, 0);
				close(prev);
			}
			if (fds[1] >= 0) {
				dup2(fds[1], 1);
				close(fds[1]);
				close(fds[0]);
			}
			jobctl = 0;
			interactive = 0;
			_exit(run(p->kids[i], F_NOFORK));
		}
		if (pid > 0) {
			if (jobctl) setpgid(pid, pgid ? pgid : pid);
			if (!pgid) pgid = pid;
			if (np < MAXPROCS) pids[np++] = pid;
		}
		if (prev >= 0) close(prev);
		if (fds[1] >= 0) close(fds[1]);
		prev = fds[0];
	}
	if (prev >= 0) close(prev);
	if (np == 0) return 1;
	i = new_job(jobctl ? pgid : 0, pids, np, p->text);
	if (bg) {
		last_bg = pids[np - 1];
		if (interactive) printf("[%d]\n", last_bg);
		if (i >= 0) set_current(i);
		return 0;
	}
	if (i < 0) return 1;
	if (jobctl) tcsetpgrp(0, pgid);
	r = wait_job(i, 1);
	return p->bang ? !r : r;
}

static int run_async(Node *n) {
	int pid;
	if (n->type == N_PIPE) return run_pipeline(n, 1);
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		in_child = 1;
		if (jobctl) setpgid(0, 0);
		child_signals(1);
		jobctl = 0;
		interactive = 0;
		_exit(run(n, 0));
	}
	if (pid > 0) {
		int j;
		if (jobctl) setpgid(pid, pid);
		j = new_job(jobctl ? pid : 0, &pid, 1, n->text ? n->text : "");
		last_bg = pid;
		if (interactive) printf("[%d]\n", pid);
		if (j >= 0) set_current(j);
	}
	return 0;
}

static void check_e(int r) {
	if (opt_e && r != 0 && cond_depth == 0 && !ctl) {
		fflush(stdout);
		exit(r);
	}
}

static int run(Node *n, int flags) {
	int r = 0, i;
	Saved sv;
	if (!n) return 0;
	switch (n->type) {
	case N_SIMPLE:
		r = run_simple(n, flags);
		break;
	case N_PIPE:
		if (n->bang) cond_depth++;
		r = run_pipeline(n, 0);
		if (n->bang) cond_depth--;
		last_status = r;
		if (!n->bang) check_e(r);
		return r;
	case N_LIST:
		for (i = 0; i < n->nkids && !ctl; i++) {
			if (n->bg[i]) r = run_async(n->kids[i]);
			else r = run(n->kids[i], i == n->nkids - 1 ? flags : 0);
			last_status = r;
		}
		return last_status;
	case N_AND:
	case N_OR:
		cond_depth++;
		r = run(n->a, 0);
		cond_depth--;
		last_status = r;
		if (ctl) return r;
		if ((n->type == N_AND) == (r == 0)) r = run(n->b, flags);
		last_status = r;
		return r;
	case N_IF:
		sv.n = 0;
		if (apply_redirs(n->redirs, &sv) < 0) {
			restore(&sv);
			return 1;
		}
		cond_depth++;
		r = run(n->a, 0);
		cond_depth--;
		if (ctl) r = last_status;
		else if (r == 0) r = run(n->b, 0);
		else if (n->c) r = run(n->c, 0);
		else r = 0;
		restore(&sv);
		break;
	case N_WHILE:
	case N_UNTIL:
		sv.n = 0;
		if (apply_redirs(n->redirs, &sv) < 0) {
			restore(&sv);
			return 1;
		}
		r = 0;
		for (;;) {
			int c;
			cond_depth++;
			c = run(n->a, 0);
			cond_depth--;
			if (ctl) break;
			if ((n->type == N_WHILE) != (c == 0)) break;
			r = run(n->b, 0);
			if (ctl == CTL_BREAK) {
				if (--ctl_n <= 0) ctl = 0;
				break;
			}
			if (ctl == CTL_CONTINUE) {
				if (--ctl_n <= 0) ctl = 0;
				else break;
			}
			if (ctl) break;
		}
		restore(&sv);
		break;
	case N_FOR: {
		Fields fs;
		sv.n = 0;
		if (apply_redirs(n->redirs, &sv) < 0) {
			restore(&sv);
			return 1;
		}
		if (n->has_in) expand_words(n->words, n->nwords, &fs);
		else {
			fs.v = NULL;
			fs.n = fs.cap = 0;
			for (i = 0; i < nparams; i++) fs_add(&fs, xstrdup(params[i]));
		}
		r = 0;
		for (i = 0; i < fs.n; i++) {
			setvar(n->name, fs.v[i], 0);
			r = run(n->b, 0);
			if (ctl == CTL_BREAK) {
				if (--ctl_n <= 0) ctl = 0;
				break;
			}
			if (ctl == CTL_CONTINUE) {
				if (--ctl_n <= 0) ctl = 0;
				else break;
			}
			if (ctl) break;
		}
		free_fields(&fs);
		restore(&sv);
		break;
	}
	case N_CASE: {
		char *w = expand_one(n->words[0]);
		CaseItem *it;
		int matched = 0;
		sv.n = 0;
		if (apply_redirs(n->redirs, &sv) < 0) {
			restore(&sv);
			return 1;
		}
		r = 0;
		for (it = n->items; it && !matched; it = it->next)
			for (i = 0; i < it->npats && !matched; i++) {
				char *pat = expand_pattern(it->pats[i]);
				if (fnmatch(pat, w, 0) == 0) {
					matched = 1;
					r = run(it->body, 0);
				}
				free(pat);
			}
		free(w);
		restore(&sv);
		break;
	}
	case N_GROUP:
		sv.n = 0;
		if (apply_redirs(n->redirs, &sv) < 0) {
			restore(&sv);
			return 1;
		}
		r = run(n->a, 0);
		restore(&sv);
		break;
	case N_SUBSHELL: {
		int pid, st;
		fflush(stdout);
		if (flags & F_NOFORK) {
			if (apply_redirs(n->redirs, NULL) < 0) _exit(1);
			_exit(run(n->a, 0));
		}
		pid = fork();
		if (pid == 0) {
			in_child = 1;
			jobctl = 0;
			interactive = 0;
			if (apply_redirs(n->redirs, NULL) < 0) _exit(1);
			r = run(n->a, 0);
			fflush(stdout);
			_exit(r);
		}
		while (waitpid(pid, &st, 0) < 0 && errno == EINTR) ;
		r = code_of(st);
		break;
	}
	case N_FUNC: {
		Func *f = findfunc(n->name);
		if (!f) {
			f = xmalloc(sizeof(Func));
			f->name = xstrdup(n->name);
			f->next = funcs;
			funcs = f;
		}
		f->body = n->a;
		r = 0;
		break;
	}
	}
	last_status = r;
	if (n->type == N_SIMPLE) check_e(r);
	return r;
}

static int run_string(const char *s) {
	Node *n = parse_program(s);
	if (!n) return perr == 2 ? (fprintf(stderr, "sh: syntax error: unexpected end of file\n"), 2) : 2;
	return run(n, 0);
}

static int job_spec(const char *s) {
	int i, best = -1;
	if (!s || !strcmp(s, "%%") || !strcmp(s, "%+") || !strcmp(s, "%")) return cur_job >= 0 && jobs[cur_job].used ? cur_job : -1;
	if (!strcmp(s, "%-")) return prev_job >= 0 && jobs[prev_job].used ? prev_job : -1;
	if (s[0] == '%') s++;
	if (*s >= '0' && *s <= '9') {
		int num = atoi(s);
		for (i = 0; i < MAXJOBS; i++)
			if (jobs[i].used && jobs[i].num == num) return i;
		return -1;
	}
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].used && !strncmp(jobs[i].text, s, strlen(s))) best = i;
	return best;
}

static int sig_number(const char *s) {
	static const char *names[] = {"", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM", "", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU", NULL};
	int i;
	if (*s >= '0' && *s <= '9') return atoi(s);
	if (!strncmp(s, "SIG", 3)) s += 3;
	for (i = 1; names[i]; i++)
		if (names[i][0] && !strcmp(s, names[i])) return i;
	return -1;
}

static int read_line(int fd, char **out) {
	int cap = 128, n = 0, r;
	char c, *b = xmalloc((unsigned)cap);
	for (;;) {
		r = read(fd, &c, 1);
		if (r <= 0) break;
		if (n + 2 > cap) {
			char *nb = xmalloc((unsigned)cap * 2);
			memcpy(nb, b, (unsigned)n);
			free(b);
			b = nb;
			cap *= 2;
		}
		b[n++] = c;
		if (c == '\n') break;
	}
	b[n] = 0;
	*out = b;
	return n == 0 && r <= 0 ? -1 : n;
}

static int b_read(char **argv, int argc) {
	int i = 1, raw = 0, n, k, vi;
	char *line, *p;
	if (i < argc && !strcmp(argv[i], "-r")) {
		raw = 1;
		i++;
	}
	fflush(stdout);
	n = read_line(0, &line);
	if (n < 0) {
		for (; i < argc; i++) setvar(argv[i], "", 0);
		free(line);
		return 1;
	}
	if (n > 0 && line[n - 1] == '\n') line[--n] = 0;
	if (!raw) {
		char *w = line, *q = line;
		while (*w) {
			if (*w == '\\' && w[1]) w++;
			*q++ = *w++;
		}
		*q = 0;
	}
	if (i >= argc) {
		setvar("REPLY", line, 0);
		free(line);
		return 0;
	}
	p = line;
	for (vi = i; vi < argc; vi++) {
		char *start;
		while (*p && is_ifs((unsigned char)*p)) p++;
		start = p;
		if (vi == argc - 1) {
			k = (int)strlen(p);
			while (k > 0 && is_ifs((unsigned char)p[k - 1])) k--;
			p[k] = 0;
			setvar(argv[vi], start, 0);
			break;
		}
		while (*p && !is_ifs((unsigned char)*p)) p++;
		if (*p) *p++ = 0;
		setvar(argv[vi], start, 0);
	}
	free(line);
	return 0;
}

static int b_cd(char **argv, int argc) {
	char buf[512], *dir, old[512];
	int print = 0;
	if (!getcwd(old, sizeof(old))) old[0] = 0;
	if (argc < 2) {
		dir = getvar("HOME");
		if (!dir) dir = "/";
	} else if (!strcmp(argv[1], "-")) {
		dir = getvar("OLDPWD");
		if (!dir) {
			fprintf(stderr, "sh: cd: OLDPWD not set\n");
			return 1;
		}
		print = 1;
	} else {
		dir = argv[1];
	}
	if (chdir(dir) < 0) {
		printf("sh: cd: cannot change directory\n");
		return 1;
	}
	setvar("OLDPWD", old, 0);
	if (getcwd(buf, sizeof(buf))) {
		setvar("PWD", buf, 0);
		if (print) printf("%s\n", buf);
	}
	return 0;
}

static int b_kill(char **argv, int argc) {
	int i = 1, sig = SIGTERM, rc = 0;
	if (argc > 1 && !strcmp(argv[1], "-l")) {
		for (i = 1; i < 23; i++)
			if (i != 16) {
				static const char *names[] = {"", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM", "", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU"};
				printf("%d %s\n", i, names[i]);
			}
		return 0;
	}
	if (argc > 2 && !strcmp(argv[1], "-s")) {
		sig = sig_number(argv[2]);
		i = 3;
	} else if (argc > 1 && argv[1][0] == '-' && argv[1][1]) {
		sig = sig_number(argv[1] + 1);
		i = 2;
	}
	if (sig < 0 || sig >= 32) {
		printf("kill: bad signal\n");
		return 1;
	}
	for (; i < argc; i++) {
		int target;
		if (argv[i][0] == '%') {
			int j = job_spec(argv[i]);
			if (j < 0) {
				printf("kill: no such job %s\n", argv[i]);
				rc = 1;
				continue;
			}
			target = jobs[j].pgid ? -jobs[j].pgid : jobs[j].pids[jobs[j].nprocs - 1];
			if (kill(target, sig) < 0) {
				fprintf(stderr, "kill: no process %s\n", argv[i]);
				rc = 1;
			} else if (jobs[j].stopped && sig != SIGCONT && sig != SIGSTOP && sig != SIGTSTP) {
				kill(target, SIGCONT);
				jobs[j].stopped = 0;
			}
			continue;
		}
		target = atoi(argv[i]);
		if (kill(target, sig) < 0) {
			fprintf(stderr, "kill: no process %s\n", argv[i]);
			rc = 1;
		}
	}
	return rc;
}

static int b_fgbg(char **argv, int argc, int fg) {
	int j = job_spec(argc > 1 ? argv[1] : NULL);
	if (j < 0) {
		printf("%s: no current job\n", fg ? "fg" : "bg");
		return 1;
	}
	if (!jobctl) {
		printf("%s: no job control\n", fg ? "fg" : "bg");
		return 1;
	}
	set_current(j);
	if (fg) {
		printf("%s\n", jobs[j].text);
		fflush(stdout);
		tcsetpgrp(0, jobs[j].pgid);
		jobs[j].stopped = 0;
		kill(-jobs[j].pgid, SIGCONT);
		return wait_job(j, 1);
	}
	jobs[j].stopped = 0;
	printf("[%d]%c %s &\n", jobs[j].num, mark(&jobs[j]), jobs[j].text);
	kill(-jobs[j].pgid, SIGCONT);
	return 0;
}

static int b_wait(char **argv, int argc) {
	int i, r = 0;
	if (argc < 2) {
		for (i = 0; i < MAXJOBS; i++)
			if (jobs[i].used) {
				while (jobs[i].alive > 0) {
					int st, pid = waitpid(-1, &st, 0);
					if (pid < 0) {
						if (errno == EINTR) continue;
						jobs[i].alive = 0;
						break;
					}
					record(pid, st);
				}
				drop_job(i);
			}
		return 0;
	}
	for (i = 1; i < argc; i++) {
		int j = -1, k;
		if (argv[i][0] == '%') j = job_spec(argv[i]);
		else {
			int pid = atoi(argv[i]);
			for (k = 0; k < MAXJOBS && j < 0; k++) {
				int m;
				if (!jobs[k].used) continue;
				for (m = 0; m < jobs[k].nprocs; m++)
					if (jobs[k].pids[m] == pid) j = k;
			}
		}
		if (j < 0) {
			r = 127;
			continue;
		}
		while (jobs[j].alive > 0) {
			int st, pid = waitpid(-1, &st, 0);
			if (pid < 0) {
				if (errno == EINTR) continue;
				jobs[j].alive = 0;
				break;
			}
			record(pid, st);
		}
		r = code_of(jobs[j].status);
		drop_job(j);
	}
	return r;
}

static int b_set(char **argv, int argc) {
	int i;
	if (argc == 1) {
		Var *v;
		for (v = vars; v; v = v->next)
			if (v->value) printf("%s=%s\n", v->name, v->value);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (!strcmp(a, "--")) {
			i++;
			break;
		}
		if ((a[0] == '-' || a[0] == '+') && a[1]) {
			int on = a[0] == '-', k;
			for (k = 1; a[k]; k++) {
				if (a[k] == 'e') opt_e = on;
				else if (a[k] == 'x') opt_x = on;
				else {
					printf("sh: set: unknown option %c\n", a[k]);
					return 1;
				}
			}
		} else {
			break;
		}
	}
	if (i < argc || (argc > 1 && !strcmp(argv[argc - 1], "--"))) {
		int k;
		char **np = xmalloc((unsigned)(argc - i + 1) * sizeof(char *));
		for (k = 0; i + k < argc; k++) np[k] = xstrdup(argv[i + k]);
		np[k] = NULL;
		params = np;
		nparams = k;
	}
	return 0;
}

static int run_file(const char *path, int must);

static int run_builtin(char **argv, int argc, int *handled) {
	char *name = argv[0];
	int i;
	*handled = 1;
	if (!strcmp(name, "cd")) return b_cd(argv, argc);
	if (!strcmp(name, "pwd")) {
		char buf[512];
		if (!getcwd(buf, sizeof(buf))) return 1;
		printf("%s\n", buf);
		return 0;
	}
	if (!strcmp(name, "exit")) {
		fflush(stdout);
		exit(argc > 1 ? atoi(argv[1]) : last_status);
	}
	if (!strcmp(name, "true") || !strcmp(name, ":")) return 0;
	if (!strcmp(name, "false")) return 1;
	if (!strcmp(name, "test") || !strcmp(name, "[")) return test_main(argc, argv);
	if (!strcmp(name, "export")) {
		if (argc == 1) {
			Var *v;
			for (v = vars; v; v = v->next)
				if (v->exported && v->value) printf("export %s=%s\n", v->name, v->value);
			return 0;
		}
		for (i = 1; i < argc; i++) {
			char *eq = strchr(argv[i], '=');
			if (eq) {
				char *n = xstrndup(argv[i], (int)(eq - argv[i]));
				setvar(n, eq + 1, 1);
				free(n);
			} else {
				setvar(argv[i], NULL, 1);
			}
		}
		return 0;
	}
	if (!strcmp(name, "unset")) {
		for (i = 1; i < argc; i++) unsetvar(argv[i]);
		return 0;
	}
	if (!strcmp(name, "set")) return b_set(argv, argc);
	if (!strcmp(name, "shift")) {
		int k = argc > 1 ? atoi(argv[1]) : 1;
		if (k > nparams) return 1;
		params += k;
		nparams -= k;
		return 0;
	}
	if (!strcmp(name, "read")) return b_read(argv, argc);
	if (!strcmp(name, "wait")) return b_wait(argv, argc);
	if (!strcmp(name, "jobs")) {
		report_jobs();
		for (i = 0; i < MAXJOBS; i++)
			if (jobs[i].used) printf("[%d]%c %s %s\n", jobs[i].num, mark(&jobs[i]), jobs[i].stopped ? "Stopped" : "Running", jobs[i].text);
		return 0;
	}
	if (!strcmp(name, "fg")) return b_fgbg(argv, argc, 1);
	if (!strcmp(name, "bg")) return b_fgbg(argv, argc, 0);
	if (!strcmp(name, "kill")) return b_kill(argv, argc);
	if (!strcmp(name, "rfork")) {
		const char *s = argc > 1 ? argv[1] : "ens";
		int fl = 0;
		for (; *s; s++) {
			if (*s == 'n') fl |= RFNAMEG;
			else if (*s == 'N') fl |= RFCNAMEG;
			else if (*s == 's') fl |= RFNOTEG;
			else if (*s == 'f') fl |= RFFDG;
			else if (*s == 'F') fl |= RFCFDG;
			else if (*s == 'e') fl |= RFENVG;
			else if (*s == 'E') fl |= RFCENVG;
			else {
				fprintf(stderr, "sh: rfork: bad flag %c\n", *s);
				return 1;
			}
		}
		if (rfork(fl) < 0) {
			fprintf(stderr, "sh: rfork: %s\n", strerror(errno));
			return 1;
		}
		return 0;
	}
	if (!strcmp(name, "umask")) {
		if (argc > 1) {
			unsigned m = 0;
			char *s = argv[1];
			for (; *s >= '0' && *s <= '7'; s++) m = m * 8 + (unsigned)(*s - '0');
			if (*s) {
				printf("sh: umask: bad mask\n");
				return 1;
			}
			umask(m);
		} else {
			unsigned m = umask(0);
			umask(m);
			printf("%04o\n", m);
		}
		return 0;
	}
	if (!strcmp(name, "source") || !strcmp(name, ".")) {
		char **op = params;
		int on = nparams, r;
		if (argc < 2) {
			printf("sh: %s: file name needed\n", name);
			return 2;
		}
		if (argc > 2) {
			params = argv + 2;
			nparams = argc - 2;
		}
		r = run_file(argv[1], 1);
		params = op;
		nparams = on;
		if (ctl == CTL_RETURN) ctl = 0;
		return r;
	}
	if (!strcmp(name, "eval")) {
		int l = 0, r;
		char *s;
		for (i = 1; i < argc; i++) l += (int)strlen(argv[i]) + 1;
		s = xmalloc((unsigned)l + 2);
		s[0] = 0;
		for (i = 1; i < argc; i++) {
			if (i > 1) strcat(s, " ");
			strcat(s, argv[i]);
		}
		{
			char *osrc = src;
			int opos = pos, ope = peeked, otok = tok;
			char *ow = tok_word;
			r = run_string(s);
			src = osrc;
			pos = opos;
			peeked = ope;
			tok = otok;
			tok_word = ow;
		}
		return r;
	}
	if (!strcmp(name, "break") || !strcmp(name, "continue")) {
		ctl = name[0] == 'b' ? CTL_BREAK : CTL_CONTINUE;
		ctl_n = argc > 1 ? atoi(argv[1]) : 1;
		if (ctl_n < 1) ctl_n = 1;
		return 0;
	}
	if (!strcmp(name, "return")) {
		ctl = CTL_RETURN;
		last_status = argc > 1 ? atoi(argv[1]) : last_status;
		return last_status;
	}
	*handled = 0;
	return 127;
}

static char *pending;
static int npending;

static void append_pending(const char *s) {
	int l = (int)strlen(s);
	char *n = xmalloc((unsigned)(npending + l + 1));
	if (npending) memcpy(n, pending, (unsigned)npending);
	memcpy(n + npending, s, (unsigned)l + 1);
	if (pending) free(pending);
	pending = n;
	npending += l;
}

static int run_lines(int fd, int prompt) {
	char *line;
	int n, r = 0;
	for (;;) {
		if (prompt) {
			report_jobs();
			printf("%s", npending ? "> " : "$ ");
			fflush(stdout);
		}
		n = read_line(fd, &line);
		if (n < 0) {
			free(line);
			if (npending) {
				fprintf(stderr, "sh: syntax error: unexpected end of file\n");
				r = 2;
			}
			break;
		}
		append_pending(line);
		free(line);
		{
			Node *t = parse_program(pending);
			if (!t && perr == 2) continue;
			if (t) r = run(t, 0);
			else r = last_status = 2;
		}
		free(pending);
		pending = NULL;
		npending = 0;
		if (ctl == CTL_RETURN) break;
		ctl = 0;
	}
	return r;
}

static int run_file(const char *path, int must) {
	int fd = open(path, O_RDONLY), r;
	char *opend = pending;
	int onp = npending;
	if (fd < 0) {
		if (must) printf("sh: cannot open %s\n", path);
		return 127;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (fd < 10) {
		int nfd = fcntl(fd, F_DUPFD, 10);
		if (nfd >= 0) {
			close(fd);
			fd = nfd;
			fcntl(fd, F_SETFD, FD_CLOEXEC);
		}
	}
	pending = NULL;
	npending = 0;
	r = run_lines(fd, 0);
	close(fd);
	pending = opend;
	npending = onp;
	return r;
}

int main(int argc, char **argv) {
	int i, r;
	char **e;
	shell_pid = getpid();
	for (e = environ; e && *e; e++) {
		char *eq = strchr(*e, '=');
		if (eq) {
			char *n = xstrndup(*e, (int)(eq - *e));
			setvar(n, eq + 1, 1);
			free(n);
		}
	}
	if (!getvar("PATH")) setvar("PATH", "/bin", 1);
	params = argv + argc;
	nparams = 0;
	cur_job = prev_job = -1;
	i = 1;
	while (i < argc && argv[i][0] == '-' && argv[i][1] && strcmp(argv[i], "-c")) {
		char *a = argv[i];
		int k;
		for (k = 1; a[k]; k++) {
			if (a[k] == 'e') opt_e = 1;
			else if (a[k] == 'x') opt_x = 1;
		}
		i++;
	}
	if (i < argc && !strcmp(argv[i], "-c")) {
		if (i + 1 >= argc) {
			printf("sh: -c needs a command\n");
			return 2;
		}
		if (i + 2 < argc) {
			arg0 = argv[i + 2];
			params = argv + i + 3;
			nparams = argc - i - 3;
		}
		r = run_string(argv[i + 1]);
		fflush(stdout);
		return r;
	}
	if (i < argc) {
		arg0 = argv[i];
		params = argv + i + 1;
		nparams = argc - i - 1;
		r = run_file(argv[i], 1);
		fflush(stdout);
		return r;
	}
	interactive = isatty(0);
	if (interactive) {
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		signal(SIGTSTP, SIG_IGN);
		signal(SIGTTIN, SIG_IGN);
		signal(SIGTTOU, SIG_IGN);
		if (tcgetpgrp(0) >= 0) {
			jobctl = 1;
			if (getpgrp() != getpid() && getsid(0) != getpid()) setpgid(0, 0);
			shell_pgid = getpgrp();
			tcsetpgrp(0, shell_pgid);
		}
	}
	r = run_lines(0, interactive);
	fflush(stdout);
	return r;
}
