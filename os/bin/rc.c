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
#include <dirent.h>
#include <fnmatch.h>
#include <glob.h>
#include <plan9.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define T_WORD 1
#define T_EOF 2
#define T_NL 3
#define T_SEMI 4
#define T_AMP 5
#define T_PIPE 6
#define T_ANDAND 7
#define T_OROR 8
#define T_BANG 9
#define T_LPAR 10
#define T_RPAR 11
#define T_LBRACE 12
#define T_RBRACE 13
#define T_REDIR 14
#define T_TWIDDLE 15
#define T_EQ 16
#define T_CARET 17

#define N_LIT 1
#define N_VAR 2
#define N_COUNT 3
#define N_JOIN 4
#define N_CAT 5
#define N_LIST 6
#define N_BQ 7
#define N_PSUB 8
#define N_SIMPLE 20
#define N_PIPE 21
#define N_AND 22
#define N_OR 23
#define N_NOT 24
#define N_BLOCK 25
#define N_IF 26
#define N_IFNOT 27
#define N_FOR 28
#define N_WHILE 29
#define N_SWITCH 30
#define N_FN 31
#define N_MATCH 32
#define N_SEQ 33
#define N_BG 34
#define N_ASSIGN 35
#define N_SUBSHELL 36

typedef struct Word {
	char *s;
	int glob;
	struct Word *next;
} Word;

typedef struct Redir {
	int type, fd, fd2;
	struct Node *target;
	struct Redir *next;
} Redir;

typedef struct Node {
	int type;
	char *s;
	int quoted, glob;
	struct Node *a, *b, *c;
	struct Node *next;
	Redir *redir;
	int fd, fd2;
} Node;

typedef struct Var {
	char *name;
	Word *val;
	struct Var *next;
} Var;

typedef struct Fn {
	char *name;
	Node *body;
	struct Fn *next;
} Fn;

typedef struct Tok {
	int type;
	Node *word;
	int rtype, fd, fd2;
	int space;
} Tok;

static char *src;
static int pos, srclen;
static Tok tok;
static Tok pushed[4];
static int npushed;
static int incomplete;
static int syntax_error;
static int quiet;
static Var *vars;
static Fn *fns;
static int interactive;
static int lastif = 1;
static int exiting;
static int breaking;
static char *cmdname = "rc";
static int tmpcount;

#define R_OUT 1
#define R_APPEND 2
#define R_IN 3
#define R_DUP 4
#define R_CLOSE 5

static void *emalloc(unsigned n) {
	void *p = calloc(1, n ? n : 1);
	if (!p) {
		fprintf(stderr, "rc: out of memory\n");
		exit(1);
	}
	return p;
}

static char *estrdup(const char *s) {
	char *d = emalloc(strlen(s) + 1);
	strcpy(d, s);
	return d;
}

static Node *node(int type) {
	Node *n = emalloc(sizeof(Node));
	n->type = type;
	return n;
}

static Word *word(const char *s, Word *next) {
	Word *w = emalloc(sizeof(Word));
	w->s = estrdup(s);
	w->next = next;
	return w;
}

static int count(Word *w) {
	int n = 0;
	for (; w; w = w->next) n++;
	return n;
}

static Word *append(Word *a, Word *b) {
	Word *w = a;
	if (!a) return b;
	while (w->next) w = w->next;
	w->next = b;
	return a;
}

static Word *copywords(Word *w) {
	Word *h = NULL, **t = &h;
	for (; w; w = w->next) {
		*t = word(w->s, NULL);
		(*t)->glob = w->glob;
		t = &(*t)->next;
	}
	return h;
}

static void freewords(Word *w) {
	while (w) {
		Word *n = w->next;
		free(w->s);
		free(w);
		w = n;
	}
}

static Var *findvar(const char *name) {
	Var *v;
	for (v = vars; v; v = v->next)
		if (!strcmp(v->name, name)) return v;
	return NULL;
}

static int envname(const char *name) {
	if (!*name || strlen(name) > 27) return 0;
	if (!strcmp(name, "*") || !strcmp(name, "status") || !strcmp(name, "pid") || !strcmp(name, "apid")) return 0;
	for (; *name; name++)
		if (*name == '/') return 0;
	return 1;
}

static void envwrite(const char *name, Word *val) {
	char path[64];
	int fd;
	if (!envname(name)) return;
	snprintf(path, sizeof(path), "/env/%s", name);
	if (!val) {
		unlink(path);
		return;
	}
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	for (; val; val = val->next) write(fd, val->s, strlen(val->s) + 1);
	close(fd);
}

static void setvar(const char *name, Word *val) {
	Var *v = findvar(name);
	if (!v) {
		v = emalloc(sizeof(Var));
		v->name = estrdup(name);
		v->next = vars;
		vars = v;
	} else freewords(v->val);
	v->val = val;
	envwrite(name, val);
}

static void setvar1(const char *name, const char *s) {
	setvar(name, s ? word(s, NULL) : NULL);
}

static Word *getvar(const char *name) {
	Var *v;
	if (name[0] >= '0' && name[0] <= '9') {
		int n = atoi(name);
		Word *w;
		if (n == 0) return word(cmdname, NULL);
		v = findvar("*");
		w = v ? v->val : NULL;
		while (w && --n > 0) w = w->next;
		return w ? word(w->s, NULL) : NULL;
	}
	v = findvar(name);
	return v ? copywords(v->val) : NULL;
}

static void import_env(char **envp) {
	DIR *d = opendir("/env");
	struct dirent *e;
	int i;
	while (d && (e = readdir(d)) != NULL) {
		char path[64], buf[4096];
		int fd, n, k;
		Word *h = NULL, **t = &h;
		Var *v;
		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof(path), "/env/%s", e->d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0) continue;
		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n < 0) continue;
		buf[n] = 0;
		for (k = 0; k < n;) {
			*t = word(buf + k, NULL);
			t = &(*t)->next;
			k += (int)strlen(buf + k) + 1;
		}
		v = emalloc(sizeof(Var));
		v->name = estrdup(e->d_name);
		v->val = h;
		v->next = vars;
		vars = v;
	}
	if (d) closedir(d);
	for (i = 0; envp && envp[i]; i++) {
		char *eq = strchr(envp[i], '='), name[64];
		unsigned l;
		if (!eq) continue;
		l = (unsigned)(eq - envp[i]);
		if (l >= sizeof(name)) continue;
		memcpy(name, envp[i], l);
		name[l] = 0;
		if (findvar(name)) continue;
		{
			Var *v = emalloc(sizeof(Var));
			v->name = estrdup(name);
			v->val = word(eq + 1, NULL);
			v->next = vars;
			vars = v;
		}
	}
}

static char **build_env(void) {
	Var *v;
	int n = 0, i = 0;
	char **env;
	for (v = vars; v; v = v->next) n++;
	env = emalloc(sizeof(char *) * (unsigned)(n + 2));
	for (v = vars; v; v = v->next) {
		unsigned len = strlen(v->name) + 2;
		Word *w;
		char *s;
		if (!envname(v->name) || !v->val) continue;
		for (w = v->val; w; w = w->next) len += strlen(w->s) + 1;
		s = emalloc(len);
		strcpy(s, v->name);
		strcat(s, "=");
		for (w = v->val; w; w = w->next) {
			strcat(s, w->s);
			if (w->next) strcat(s, " ");
		}
		env[i++] = s;
	}
	env[i] = NULL;
	return env;
}

static int truestatus(void) {
	Var *v = findvar("status");
	Word *w;
	if (!v) return 1;
	for (w = v->val; w; w = w->next)
		if (w->s[0] && strcmp(w->s, "0")) return 0;
	return 1;
}

static void setstatus(const char *s) {
	setvar1("status", s);
}

static int special(int c) {
	return c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' || c == '^' || c == '$' || c == '`' || c == '\'' || c == '{' || c == '}' || c == '(' || c == ')' || c == '<' || c == '>' || c == '=' || c == 0;
}

static int namechar(int c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static void synerr(const char *what) {
	if (!syntax_error && !quiet) fprintf(stderr, "rc: syntax error: %s\n", what);
	syntax_error = 1;
}

static int bracket(int *fd, int *fd2) {
	int a = 0, b = -1;
	if (src[pos] != '[') return 0;
	pos++;
	while (src[pos] >= '0' && src[pos] <= '9') a = a * 10 + (src[pos++] - '0');
	if (src[pos] == '=') {
		pos++;
		if (src[pos] == ']') b = -2;
		else {
			b = 0;
			while (src[pos] >= '0' && src[pos] <= '9') b = b * 10 + (src[pos++] - '0');
		}
	}
	if (src[pos] != ']') {
		synerr("bad [n]");
		return 0;
	}
	pos++;
	*fd = a;
	*fd2 = b;
	return 1;
}

static Node *parse_list(int stop);

static Node *sub_body(void) {
	Node *body;
	Tok save = tok, savep[4];
	int saven = npushed;
	memcpy(savep, pushed, sizeof(pushed));
	npushed = 0;
	body = parse_list(T_RBRACE);
	if (tok.type != T_RBRACE) synerr("missing }");
	tok = save;
	npushed = saven;
	memcpy(pushed, savep, sizeof(pushed));
	return body;
}

static void lex(void) {
	int c, start;
	tok.word = NULL;
	tok.space = 0;
	for (;;) {
		c = src[pos];
		if (c == ' ' || c == '\t') {
			tok.space = 1;
			pos++;
			continue;
		}
		if (c == '#') {
			while (src[pos] && src[pos] != '\n') pos++;
			continue;
		}
		break;
	}
	c = src[pos];
	if (!c) {
		tok.type = T_EOF;
		return;
	}
	pos++;
	switch (c) {
	case '\n':
		tok.type = T_NL;
		return;
	case ';':
		tok.type = T_SEMI;
		return;
	case '^':
		tok.type = T_CARET;
		return;
	case '=':
		tok.type = T_EQ;
		return;
	case '(':
		tok.type = T_LPAR;
		return;
	case ')':
		tok.type = T_RPAR;
		return;
	case '{':
		tok.type = T_LBRACE;
		return;
	case '}':
		tok.type = T_RBRACE;
		return;
	case '~':
		tok.type = T_TWIDDLE;
		return;
	case '!':
		tok.type = T_BANG;
		return;
	case '&':
		if (src[pos] == '&') {
			pos++;
			tok.type = T_ANDAND;
		} else tok.type = T_AMP;
		return;
	case '|':
		if (src[pos] == '|') {
			pos++;
			tok.type = T_OROR;
			return;
		}
		tok.type = T_PIPE;
		tok.fd = 1;
		tok.fd2 = 0;
		if (bracket(&tok.fd, &tok.fd2) && tok.fd2 < 0) tok.fd2 = 0;
		return;
	case '>':
	case '<':
		if (c == '<' && src[pos] == '{') {
			pos++;
			tok.type = T_WORD;
			tok.word = node(N_PSUB);
			tok.word->a = sub_body();
			return;
		}
		tok.type = T_REDIR;
		tok.rtype = c == '<' ? R_IN : R_OUT;
		if (c == '>' && src[pos] == '>') {
			pos++;
			tok.rtype = R_APPEND;
		}
		tok.fd = c == '<' ? 0 : 1;
		tok.fd2 = -1;
		if (bracket(&tok.fd, &tok.fd2)) {
			if (tok.fd2 == -2) tok.rtype = R_CLOSE;
			else if (tok.fd2 >= 0) tok.rtype = R_DUP;
		}
		return;
	case '`':
		if (src[pos] != '{') {
			synerr("` without {");
			tok.type = T_EOF;
			return;
		}
		pos++;
		tok.type = T_WORD;
		tok.word = node(N_BQ);
		tok.word->a = sub_body();
		return;
	case '$': {
		int kind = N_VAR;
		if (src[pos] == '#') {
			kind = N_COUNT;
			pos++;
		} else if (src[pos] == '"') {
			kind = N_JOIN;
			pos++;
		}
		start = pos;
		if (src[pos] == '*') pos++;
		else
			while (namechar(src[pos])) pos++;
		if (pos == start) {
			synerr("bad $");
			tok.type = T_EOF;
			return;
		}
		tok.type = T_WORD;
		tok.word = node(kind);
		tok.word->s = emalloc((unsigned)(pos - start + 1));
		memcpy(tok.word->s, src + start, (unsigned)(pos - start));
		return;
	}
	case '\'': {
		char *buf = emalloc((unsigned)srclen + 1);
		int n = 0;
		for (;;) {
			if (!src[pos]) {
				incomplete = 1;
				synerr("unterminated quote");
				tok.type = T_EOF;
				return;
			}
			if (src[pos] == '\'') {
				if (src[pos + 1] == '\'') {
					buf[n++] = '\'';
					pos += 2;
					continue;
				}
				pos++;
				break;
			}
			buf[n++] = src[pos++];
		}
		buf[n] = 0;
		tok.type = T_WORD;
		tok.word = node(N_LIT);
		tok.word->s = buf;
		tok.word->quoted = 1;
		return;
	}
	}
	start = pos - 1;
	while (!special(src[pos])) pos++;
	tok.type = T_WORD;
	tok.word = node(N_LIT);
	tok.word->s = emalloc((unsigned)(pos - start + 1));
	memcpy(tok.word->s, src + start, (unsigned)(pos - start));
	{
		char *s;
		for (s = tok.word->s; *s; s++)
			if (*s == '*' || *s == '?' || *s == '[') tok.word->glob = 1;
	}
}

static void next(void) {
	if (npushed) {
		tok = pushed[--npushed];
		return;
	}
	lex();
}

static void unread(void) {
	if (npushed < 4) pushed[npushed++] = tok;
}

static int is_kw(const char *kw) {
	return tok.type == T_WORD && tok.word->type == N_LIT && !tok.word->quoted && !strcmp(tok.word->s, kw);
}

static Node *parse_cmd(void);
static Node *parse_andor(void);

static void skipnl(void) {
	for (;;) {
		next();
		if (tok.type != T_NL) break;
	}
	unread();
}

static Node *parse_word(void);

static Node *atom(void) {
	Node *n;
	if (tok.type == T_WORD) return tok.word;
	if (tok.type == T_EQ || tok.type == T_TWIDDLE || tok.type == T_BANG) {
		n = node(N_LIT);
		n->s = estrdup(tok.type == T_EQ ? "=" : tok.type == T_TWIDDLE ? "~" : "!");
		return n;
	}
	if (tok.type == T_LPAR) {
		Node *h = NULL, **t = &h;
		n = node(N_LIST);
		for (;;) {
			next();
			if (tok.type == T_NL) continue;
			if (tok.type == T_RPAR) break;
			if (tok.type == T_EOF) {
				incomplete = 1;
				synerr("missing )");
				return n;
			}
			if (tok.type != T_WORD && tok.type != T_LPAR && tok.type != T_EQ && tok.type != T_TWIDDLE && tok.type != T_BANG) {
				synerr("bad word in list");
				return n;
			}
			*t = parse_word();
			t = &(*t)->next;
		}
		n->a = h;
		return n;
	}
	synerr("word expected");
	return node(N_LIT);
}

static Node *parse_word(void) {
	Node *w = atom();
	for (;;) {
		next();
		if (tok.type == T_CARET) {
			Node *c = node(N_CAT);
			next();
			c->a = w;
			c->b = atom();
			w = c;
			continue;
		}
		if (!tok.space && (tok.type == T_WORD || tok.type == T_EQ || (tok.type == T_LPAR && w->type != N_LIT))) {
			Node *c = node(N_CAT);
			c->a = w;
			c->b = atom();
			w = c;
			continue;
		}
		unread();
		return w;
	}
}

static int terminator(int t) {
	return t == T_SEMI || t == T_AMP || t == T_NL || t == T_PIPE || t == T_ANDAND || t == T_OROR || t == T_RBRACE || t == T_RPAR || t == T_EOF;
}

static Redir *parse_redirs(Redir *r) {
	Redir **t = &r;
	while (*t) t = &(*t)->next;
	for (;;) {
		next();
		if (tok.type != T_REDIR) {
			unread();
			return r;
		}
		*t = emalloc(sizeof(Redir));
		(*t)->type = tok.rtype;
		(*t)->fd = tok.fd;
		(*t)->fd2 = tok.fd2;
		if (tok.rtype != R_DUP && tok.rtype != R_CLOSE) {
			next();
			if (terminator(tok.type)) {
				synerr("redirection without a file");
				unread();
				return r;
			}
			(*t)->target = parse_word();
		}
		t = &(*t)->next;
	}
}

static Node *expect_paren_list(void) {
	Node *l;
	next();
	if (tok.type != T_LPAR) {
		synerr("( expected");
		unread();
		return NULL;
	}
	l = parse_list(T_RPAR);
	if (tok.type != T_RPAR) synerr(") expected");
	return l;
}

static Node *body_cmd(void) {
	skipnl();
	return parse_cmd();
}

static Node *parse_simple(void) {
	Node *n = node(N_SIMPLE), *h = NULL, **t = &h, *ah = NULL, **at = &ah;
	int words = 0;
	for (;;) {
		next();
		if (terminator(tok.type)) {
			unread();
			break;
		}
		if (tok.type == T_REDIR) {
			unread();
			n->redir = parse_redirs(n->redir);
			continue;
		}
		if (!words && tok.type == T_WORD && tok.word->type == N_LIT && !tok.word->quoted) {
			Tok nt = tok;
			Node *name = tok.word;
			const char *s = name->s;
			int ok = *s != 0;
			for (; *s; s++)
				if (!namechar(*s) && *s != '*') ok = 0;
			if (ok) {
				next();
				if (tok.type == T_EQ && !tok.space) {
					Node *a = node(N_ASSIGN);
					a->s = name->s;
					next();
					if (terminator(tok.type) || tok.space || tok.type == T_REDIR) {
						unread();
						a->a = NULL;
					} else a->a = parse_word();
					*at = a;
					at = &a->next;
					continue;
				}
				unread();
				tok = nt;
			}
		}
		*t = parse_word();
		t = &(*t)->next;
		words++;
	}
	n->a = h;
	n->c = ah;
	if (!h && !ah && !n->redir) {
		synerr("empty command");
	}
	return n;
}

static Node *parse_cmd(void) {
	Node *n;
	next();
	if (tok.type == T_EOF) {
		incomplete = 1;
		synerr("unexpected end of input");
		return node(N_BLOCK);
	}
	if (tok.type == T_BANG) {
		n = node(N_NOT);
		n->a = parse_cmd();
		return n;
	}
	if (tok.type == T_LBRACE) {
		n = node(N_BLOCK);
		n->a = parse_list(T_RBRACE);
		if (tok.type != T_RBRACE) {
			incomplete = 1;
			synerr("missing }");
		}
		n->redir = parse_redirs(NULL);
		return n;
	}
	if (tok.type == T_LPAR) {
		unread();
		return parse_simple();
	}
	if (tok.type == T_TWIDDLE) {
		Node *h = NULL, **t = &h;
		n = node(N_MATCH);
		for (;;) {
			next();
			if (terminator(tok.type)) {
				unread();
				break;
			}
			*t = parse_word();
			t = &(*t)->next;
		}
		n->a = h;
		if (!h) synerr("~ needs a subject");
		return n;
	}
	if (is_kw("if")) {
		next();
		if (is_kw("not")) {
			n = node(N_IFNOT);
			n->a = body_cmd();
			return n;
		}
		unread();
		n = node(N_IF);
		n->a = expect_paren_list();
		n->b = body_cmd();
		return n;
	}
	if (is_kw("while")) {
		n = node(N_WHILE);
		n->a = expect_paren_list();
		n->b = body_cmd();
		return n;
	}
	if (is_kw("for")) {
		n = node(N_FOR);
		next();
		if (tok.type != T_LPAR) {
			synerr("( expected after for");
			return n;
		}
		next();
		if (tok.type != T_WORD || tok.word->type != N_LIT) {
			synerr("for needs a variable");
			return n;
		}
		n->s = tok.word->s;
		next();
		if (is_kw("in")) {
			Node *h = NULL, **t = &h;
			for (;;) {
				next();
				if (tok.type == T_RPAR) break;
				if (tok.type == T_EOF || tok.type == T_NL) {
					incomplete = tok.type == T_EOF;
					synerr(") expected in for");
					return n;
				}
				*t = parse_word();
				t = &(*t)->next;
			}
			n->a = h;
			n->fd = 1;
		} else if (tok.type != T_RPAR) {
			synerr(") expected in for");
			return n;
		}
		n->b = body_cmd();
		return n;
	}
	if (is_kw("switch")) {
		n = node(N_SWITCH);
		next();
		if (tok.type != T_LPAR) {
			synerr("( expected after switch");
			return n;
		}
		next();
		n->a = parse_word();
		next();
		if (tok.type != T_RPAR) {
			synerr(") expected in switch");
			return n;
		}
		skipnl();
		next();
		if (tok.type != T_LBRACE) {
			synerr("{ expected in switch");
			return n;
		}
		n->b = parse_list(T_RBRACE);
		if (tok.type != T_RBRACE) {
			incomplete = 1;
			synerr("missing } in switch");
		}
		return n;
	}
	if (is_kw("fn")) {
		n = node(N_FN);
		next();
		if (tok.type != T_WORD || tok.word->type != N_LIT) {
			synerr("fn needs a name");
			return n;
		}
		n->s = tok.word->s;
		next();
		if (tok.type == T_LBRACE) {
			n->a = parse_list(T_RBRACE);
			if (tok.type != T_RBRACE) {
				incomplete = 1;
				synerr("missing } in fn");
			}
			n->fd = 1;
		} else unread();
		return n;
	}
	unread();
	return parse_simple();
}

static Node *parse_pipe(void) {
	Node *n = parse_cmd();
	for (;;) {
		Node *p;
		next();
		if (tok.type != T_PIPE) {
			unread();
			return n;
		}
		p = node(N_PIPE);
		p->fd = tok.fd;
		p->fd2 = tok.fd2;
		p->a = n;
		skipnl();
		p->b = parse_cmd();
		n = p;
	}
}

static Node *parse_andor(void) {
	Node *n = parse_pipe();
	for (;;) {
		Node *p;
		next();
		if (tok.type != T_ANDAND && tok.type != T_OROR) {
			unread();
			return n;
		}
		p = node(tok.type == T_ANDAND ? N_AND : N_OR);
		p->a = n;
		skipnl();
		p->b = parse_pipe();
		n = p;
	}
}

static Node *parse_list(int stop) {
	Node *h = NULL, **t = &h;
	for (;;) {
		Node *c;
		next();
		if (tok.type == T_NL || tok.type == T_SEMI) continue;
		if (tok.type == stop) return h;
		if (tok.type == T_EOF) {
			if (stop != T_EOF) incomplete = 1;
			return h;
		}
		unread();
		c = parse_andor();
		if (syntax_error) return h;
		next();
		if (tok.type == T_AMP) {
			Node *b = node(N_BG);
			b->a = c;
			c = b;
		} else unread();
		*t = c;
		t = &c->next;
	}
}

static void run(Node *n);
static void run_list(Node *n);
static int run_text(char *text, int checkonly);

typedef struct Temp {
	char *path;
	struct Temp *next;
} Temp;

static Temp *temps;

static Word *eval(Node *n);

static char *status_code_str(int st, char *buf) {
	if (WIFEXITED(st)) {
		if (WEXITSTATUS(st) == 0) buf[0] = 0;
		else sprintf(buf, "%d", WEXITSTATUS(st));
	} else if (WIFSIGNALED(st)) sprintf(buf, "sig%d", WTERMSIG(st));
	else buf[0] = 0;
	return buf;
}

static int exit_code(void) {
	Var *v = findvar("status");
	if (truestatus()) return 0;
	if (v && v->val && atoi(v->val->s) > 0) return atoi(v->val->s) & 255;
	return 1;
}

static Word *substitute(Node *body) {
	int p[2], pid, n, cap = 256, len = 0, st;
	char *buf = emalloc((unsigned)cap), *ifs;
	Word *h = NULL, **t = &h, *iw;
	char ifsbuf[16];
	if (pipe(p) < 0) return NULL;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		close(p[0]);
		dup2(p[1], 1);
		close(p[1]);
		run_list(body);
		fflush(stdout);
		_exit(exit_code());
	}
	close(p[1]);
	for (;;) {
		if (len + 128 > cap) {
			cap *= 2;
			buf = realloc(buf, (unsigned)cap);
		}
		n = read(p[0], buf + len, (unsigned)(cap - len - 1));
		if (n <= 0) break;
		len += n;
	}
	close(p[0]);
	waitpid(pid, &st, 0);
	buf[len] = 0;
	iw = getvar("ifs");
	if (iw) {
		strncpy(ifsbuf, iw->s, sizeof(ifsbuf) - 1);
		ifsbuf[sizeof(ifsbuf) - 1] = 0;
		ifs = ifsbuf;
		freewords(iw);
	} else ifs = " \t\n";
	{
		char *s = buf;
		for (;;) {
			char *e;
			while (*s && strchr(ifs, *s)) s++;
			if (!*s) break;
			e = s;
			while (*e && !strchr(ifs, *e)) e++;
			{
				char c = *e;
				*e = 0;
				*t = word(s, NULL);
				t = &(*t)->next;
				*e = c;
			}
			s = e;
		}
	}
	free(buf);
	return h;
}

static Word *psub(Node *body) {
	char path[64];
	int fd, pid, st;
	Temp *tp;
	snprintf(path, sizeof(path), "/tmp/rc%d.%d", getpid(), tmpcount++);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return NULL;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		dup2(fd, 1);
		close(fd);
		run_list(body);
		fflush(stdout);
		_exit(exit_code());
	}
	close(fd);
	waitpid(pid, &st, 0);
	tp = emalloc(sizeof(Temp));
	tp->path = estrdup(path);
	tp->next = temps;
	temps = tp;
	return word(path, NULL);
}

static void clean_temps(void) {
	while (temps) {
		Temp *t = temps->next;
		unlink(temps->path);
		free(temps->path);
		free(temps);
		temps = t;
	}
}

static Word *concat(Word *a, Word *b, int *err) {
	Word *h = NULL, **t = &h, *x, *y;
	int na = count(a), nb = count(b);
	if (na == 0 || nb == 0) {
		if (na == 0 && nb == 0) return NULL;
		*err = 1;
		fprintf(stderr, "rc: null list in concatenation\n");
		return NULL;
	}
	if (na != nb && na != 1 && nb != 1) {
		*err = 1;
		fprintf(stderr, "rc: mismatched list lengths in concatenation\n");
		return NULL;
	}
	for (x = a, y = b; x || y;) {
		Word *xa = x ? x : a, *yb = y ? y : b;
		char *s = emalloc(strlen(xa->s) + strlen(yb->s) + 1);
		strcpy(s, xa->s);
		strcat(s, yb->s);
		*t = emalloc(sizeof(Word));
		(*t)->s = s;
		(*t)->glob = xa->glob || yb->glob;
		t = &(*t)->next;
		if (na == nb) {
			x = x->next;
			y = y->next;
		} else if (na == 1) {
			y = y->next;
			x = y ? a : NULL;
			if (!y) break;
		} else {
			x = x->next;
			y = x ? b : NULL;
			if (!x) break;
		}
	}
	return h;
}

static int eval_error;

static Word *eval1(Node *n) {
	Word *w, *r;
	char buf[16];
	switch (n->type) {
	case N_LIT:
		w = word(n->s, NULL);
		w->glob = n->glob && !n->quoted;
		return w;
	case N_VAR:
		return getvar(n->s);
	case N_COUNT:
		w = getvar(n->s);
		sprintf(buf, "%d", count(w));
		freewords(w);
		return word(buf, NULL);
	case N_JOIN: {
		unsigned len = 1;
		char *s;
		w = getvar(n->s);
		for (r = w; r; r = r->next) len += strlen(r->s) + 1;
		s = emalloc(len);
		for (r = w; r; r = r->next) {
			strcat(s, r->s);
			if (r->next) strcat(s, " ");
		}
		freewords(w);
		r = word(s, NULL);
		free(s);
		return r;
	}
	case N_LIST: {
		Node *e;
		Word *h = NULL;
		for (e = n->a; e; e = e->next) h = append(h, eval1(e));
		return h;
	}
	case N_CAT: {
		Word *a = eval1(n->a), *b = eval1(n->b);
		r = concat(a, b, &eval_error);
		freewords(a);
		freewords(b);
		return r;
	}
	case N_BQ:
		return substitute(n->a);
	case N_PSUB:
		return psub(n->a);
	}
	return NULL;
}

static Word *globbed(Word *w) {
	Word *h = NULL, **t = &h;
	while (w) {
		Word *nx = w->next;
		glob_t g;
		w->next = NULL;
		if (w->glob && glob(w->s, 0, NULL, &g) == 0 && g.gl_pathc > 0) {
			unsigned i;
			for (i = 0; i < g.gl_pathc; i++) {
				*t = word(g.gl_pathv[i], NULL);
				t = &(*t)->next;
			}
			globfree(&g);
			freewords(w);
		} else {
			*t = w;
			t = &w->next;
		}
		w = nx;
	}
	return h;
}

static Word *eval(Node *n) {
	Word *h = NULL;
	for (; n; n = n->next) h = append(h, globbed(eval1(n)));
	return h;
}

static Fn *findfn(const char *name) {
	Fn *f;
	for (f = fns; f; f = f->next)
		if (!strcmp(f->name, name)) return f;
	return NULL;
}

typedef struct Saved {
	int fd, copy;
} Saved;

static int apply_redirs(Redir *r, Saved *save, int *nsave) {
	for (; r; r = r->next) {
		int fd = -1;
		if (save) {
			save[*nsave].fd = r->fd;
			save[*nsave].copy = dup(r->fd);
			(*nsave)++;
		}
		if (r->type == R_DUP) {
			if (dup2(r->fd2, r->fd) < 0) {
				fprintf(stderr, "rc: bad redirection [%d=%d]\n", r->fd, r->fd2);
				return -1;
			}
			continue;
		}
		if (r->type == R_CLOSE) {
			close(r->fd);
			continue;
		}
		{
			Word *w = eval(r->target);
			if (count(w) != 1) {
				fprintf(stderr, "rc: redirection needs one file name\n");
				freewords(w);
				return -1;
			}
			if (r->type == R_IN) fd = open(w->s, O_RDONLY);
			else fd = open(w->s, O_WRONLY | O_CREAT | (r->type == R_APPEND ? O_APPEND : O_TRUNC), 0666);
			if (fd < 0) {
				fprintf(stderr, "rc: %s: %s\n", w->s, strerror(errno));
				freewords(w);
				return -1;
			}
			freewords(w);
		}
		if (fd != r->fd) {
			dup2(fd, r->fd);
			close(fd);
		}
	}
	return 0;
}

static void restore(Saved *save, int nsave) {
	while (nsave-- > 0) {
		if (save[nsave].copy >= 0) {
			dup2(save[nsave].copy, save[nsave].fd);
			close(save[nsave].copy);
		} else close(save[nsave].fd);
	}
}

static void exec_external(Word *args) {
	char **argv, **env, path[512];
	int n = count(args), i = 0;
	Word *w, *pw;
	argv = emalloc(sizeof(char *) * (unsigned)(n + 1));
	for (w = args; w; w = w->next) argv[i++] = w->s;
	argv[i] = NULL;
	env = build_env();
	if (strchr(argv[0], '/')) {
		execve(argv[0], argv, env);
		fprintf(stderr, "rc: %s: %s\n", argv[0], strerror(errno));
		_exit(errno == ENOENT ? 127 : 126);
	}
	pw = getvar("path");
	if (!pw) pw = word("/bin", NULL);
	for (w = pw; w; w = w->next) {
		if (!strcmp(w->s, ".") || !w->s[0]) snprintf(path, sizeof(path), "%s", argv[0]);
		else snprintf(path, sizeof(path), "%s/%s", w->s, argv[0]);
		execve(path, argv, env);
	}
	fprintf(stderr, "rc: %s: not found\n", argv[0]);
	_exit(127);
}

static void waitfor(int pid) {
	int st;
	char buf[32];
	if (waitpid(pid, &st, 0) == pid) setstatus(status_code_str(st, buf));
}

static void print_quoted(const char *s) {
	const char *p;
	int need = !*s;
	for (p = s; *p; p++)
		if (special(*p) || *p == '#' || *p == '*' || *p == '?' || *p == '[' || *p == '~' || *p == '!') need = 1;
	if (!need) {
		printf("%s", s);
		return;
	}
	putchar('\'');
	for (p = s; *p; p++) {
		if (*p == '\'') putchar('\'');
		putchar(*p);
	}
	putchar('\'');
}

static int builtin(Word *args) {
	const char *c = args->s;
	Word *a = args->next;
	if (!strcmp(c, "cd")) {
		Word *h = a ? NULL : getvar("home");
		const char *d = a ? a->s : h ? h->s : "/";
		if (chdir(d) < 0) {
			fprintf(stderr, "rc: cd: %s: %s\n", d, strerror(errno));
			setstatus("cd failed");
		} else setstatus("");
		freewords(h);
		return 1;
	}
	if (!strcmp(c, "exit")) {
		setstatus(a ? a->s : "");
		exiting = 1;
		return 1;
	}
	if (!strcmp(c, "wait")) {
		if (a) waitfor(atoi(a->s));
		else {
			int st;
			while (wait(&st) > 0) ;
			setstatus("");
		}
		return 1;
	}
	if (!strcmp(c, "shift")) {
		Var *v = findvar("*");
		int n = a ? atoi(a->s) : 1;
		Word *w = v ? v->val : NULL;
		while (n-- > 0 && w) {
			Word *nx = w->next;
			free(w->s);
			free(w);
			w = nx;
		}
		setvar("*", w);
		if (v) v->val = w;
		setstatus("");
		return 1;
	}
	if (!strcmp(c, "whatis")) {
		int bad = 0;
		for (; a; a = a->next) {
			Var *v = findvar(a->s);
			if (findfn(a->s)) printf("fn %s\n", a->s);
			else if (v) {
				Word *w;
				printf("%s=", a->s);
				if (count(v->val) == 1) print_quoted(v->val->s);
				else {
					putchar('(');
					for (w = v->val; w; w = w->next) {
						print_quoted(w->s);
						if (w->next) putchar(' ');
					}
					putchar(')');
				}
				putchar('\n');
			} else if (!strcmp(a->s, "cd") || !strcmp(a->s, "exit") || !strcmp(a->s, "wait") || !strcmp(a->s, "whatis") || !strcmp(a->s, ".") || !strcmp(a->s, "rfork") || !strcmp(a->s, "shift") || !strcmp(a->s, "eval")) printf("builtin %s\n", a->s);
			else {
				Word *pw = getvar("path"), *w;
				char path[512];
				struct stat st;
				int found = 0;
				for (w = pw; w && !found; w = w->next) {
					snprintf(path, sizeof(path), "%s/%s", w->s, a->s);
					if (stat(path, &st) == 0 && (st.st_mode & 0111)) {
						printf("%s\n", path);
						found = 1;
					}
				}
				freewords(pw);
				if (!found) {
					fprintf(stderr, "%s: not found\n", a->s);
					bad = 1;
				}
			}
		}
		fflush(stdout);
		setstatus(bad ? "not found" : "");
		return 1;
	}
	if (!strcmp(c, "rfork")) {
		const char *s = a ? a->s : "ens";
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
				fprintf(stderr, "rc: rfork: bad flag %c\n", *s);
				setstatus("bad flag");
				return 1;
			}
		}
		if (rfork(fl) < 0) {
			fprintf(stderr, "rc: rfork: %s\n", strerror(errno));
			setstatus("rfork failed");
		} else setstatus("");
		return 1;
	}
	if (!strcmp(c, "eval")) {
		unsigned len = 1;
		Word *w;
		char *s;
		for (w = a; w; w = w->next) len += strlen(w->s) + 1;
		s = emalloc(len);
		for (w = a; w; w = w->next) {
			strcat(s, w->s);
			if (w->next) strcat(s, " ");
		}
		run_text(s, 0);
		return 1;
	}
	if (!strcmp(c, ".")) {
		int fd, n, cap = 1024, len = 0;
		char *buf;
		Var *sv;
		Word *old;
		if (!a) {
			fprintf(stderr, "rc: . needs a file\n");
			setstatus("usage");
			return 1;
		}
		fd = open(a->s, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "rc: %s: %s\n", a->s, strerror(errno));
			setstatus("cannot open");
			return 1;
		}
		buf = emalloc((unsigned)cap);
		while ((n = read(fd, buf + len, (unsigned)(cap - len - 1))) > 0) {
			len += n;
			if (len + 1 >= cap) {
				cap *= 2;
				buf = realloc(buf, (unsigned)cap);
			}
		}
		close(fd);
		buf[len] = 0;
		sv = findvar("*");
		old = sv ? copywords(sv->val) : NULL;
		setvar("*", copywords(a->next));
		run_text(buf, 0);
		setvar("*", old);
		return 1;
	}
	return 0;
}

static void run_simple(Node *n) {
	Word *args = eval(n->a);
	Node *as;
	Fn *f;
	Saved save[16];
	int nsave = 0, pid;
	typedef struct Old {
		char *name;
		Word *val;
		int had;
	} Old;
	Old old[8];
	int nold = 0;
	if (eval_error) {
		eval_error = 0;
		setstatus("bad concatenation");
		freewords(args);
		return;
	}
	if (!args) {
		for (as = n->c; as; as = as->next) setvar(as->s, eval(as->a));
		if (n->redir) {
			if (apply_redirs(n->redir, save, &nsave) < 0) setstatus("redirection failed");
			restore(save, nsave);
		}
		return;
	}
	for (as = n->c; as && nold < 8; as = as->next) {
		Var *v = findvar(as->s);
		old[nold].name = as->s;
		old[nold].had = v != NULL;
		old[nold].val = v ? copywords(v->val) : NULL;
		nold++;
		setvar(as->s, eval(as->a));
	}
	if (!strcmp(args->s, "case")) setstatus("");
	else if ((f = findfn(args->s)) != NULL) {
		Var *sv = findvar("*");
		Word *oldargs = sv ? copywords(sv->val) : NULL;
		if (apply_redirs(n->redir, save, &nsave) == 0) {
			setvar("*", copywords(args->next));
			run_list(f->body);
			setvar("*", oldargs);
		} else setstatus("redirection failed");
		restore(save, nsave);
	} else if (!strcmp(args->s, "cd") || !strcmp(args->s, "exit") || !strcmp(args->s, "wait") || !strcmp(args->s, "whatis") || !strcmp(args->s, ".") || !strcmp(args->s, "rfork") || !strcmp(args->s, "shift") || !strcmp(args->s, "eval")) {
		if (apply_redirs(n->redir, save, &nsave) == 0) builtin(args);
		else setstatus("redirection failed");
		fflush(stdout);
		restore(save, nsave);
	} else {
		fflush(stdout);
		pid = fork();
		if (pid == 0) {
			signal(SIGINT, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);
			if (apply_redirs(n->redir, NULL, NULL) < 0) _exit(1);
			exec_external(args);
		}
		if (pid < 0) setstatus("fork failed");
		else waitfor(pid);
	}
	while (nold-- > 0) {
		if (old[nold].had) setvar(old[nold].name, old[nold].val);
		else setvar(old[nold].name, NULL);
	}
	freewords(args);
	clean_temps();
}

static int match_any(Word *subject, Node *pats) {
	Word *p = NULL, *s, *q;
	Node *e;
	for (e = pats; e; e = e->next) p = append(p, eval1(e));
	int m = 0;
	for (s = subject; s && !m; s = s->next)
		for (q = p; q && !m; q = q->next)
			if (fnmatch(q->s, s->s, 0) == 0) m = 1;
	freewords(p);
	return m;
}

static void run_pipe(Node *n) {
	int p[2], lpid, rpid, st;
	char b1[32], b2[32];
	if (pipe(p) < 0) {
		setstatus("pipe failed");
		return;
	}
	fflush(stdout);
	lpid = fork();
	if (lpid == 0) {
		close(p[0]);
		dup2(p[1], n->fd);
		close(p[1]);
		run(n->a);
		fflush(stdout);
		_exit(exit_code());
	}
	rpid = fork();
	if (rpid == 0) {
		close(p[1]);
		dup2(p[0], n->fd2);
		close(p[0]);
		run(n->b);
		fflush(stdout);
		_exit(exit_code());
	}
	close(p[0]);
	close(p[1]);
	waitpid(lpid, &st, 0);
	status_code_str(st, b1);
	waitpid(rpid, &st, 0);
	status_code_str(st, b2);
	if (!b1[0] && !b2[0]) setstatus("");
	else {
		char s[80];
		snprintf(s, sizeof(s), "%s|%s", b1, b2);
		setstatus(s);
	}
}

static void run(Node *n) {
	Saved save[16];
	int nsave = 0;
	if (!n || exiting) return;
	switch (n->type) {
	case N_SIMPLE:
		run_simple(n);
		return;
	case N_PIPE:
		run_pipe(n);
		return;
	case N_AND:
		run(n->a);
		if (truestatus()) run(n->b);
		return;
	case N_OR:
		run(n->a);
		if (!truestatus()) run(n->b);
		return;
	case N_NOT:
		run(n->a);
		setstatus(truestatus() ? "false" : "");
		return;
	case N_BLOCK:
		if (apply_redirs(n->redir, save, &nsave) == 0) run_list(n->a);
		else setstatus("redirection failed");
		fflush(stdout);
		restore(save, nsave);
		return;
	case N_IF:
		run_list(n->a);
		lastif = truestatus();
		if (lastif) run(n->b);
		return;
	case N_IFNOT:
		if (!lastif) run(n->a);
		return;
	case N_FOR: {
		Word *vals, *w;
		if (n->fd) vals = eval(n->a);
		else vals = getvar("*");
		for (w = vals; w && !exiting; w = w->next) {
			setvar(n->s, word(w->s, NULL));
			run(n->b);
		}
		freewords(vals);
		return;
	}
	case N_WHILE:
		for (;;) {
			run_list(n->a);
			if (!truestatus() || exiting) break;
			run(n->b);
		}
		setstatus("");
		return;
	case N_SWITCH: {
		Word *subj = eval(n->a);
		Node *c;
		int on = 0, done = 0;
		for (c = n->b; c && !exiting; c = c->next) {
			int iscase = c->type == N_SIMPLE && c->a && c->a->type == N_LIT && !c->a->quoted && !strcmp(c->a->s, "case");
			if (iscase) {
				if (done) break;
				on = match_any(subj, c->a->next);
				if (on) done = 1;
				continue;
			}
			if (on) run(c);
		}
		freewords(subj);
		return;
	}
	case N_FN: {
		Fn *f = findfn(n->s), **pp;
		if (!n->fd) {
			for (pp = &fns; *pp; pp = &(*pp)->next)
				if (*pp == f) {
					*pp = f->next;
					break;
				}
			return;
		}
		if (!f) {
			f = emalloc(sizeof(Fn));
			f->name = estrdup(n->s);
			f->next = fns;
			fns = f;
		}
		f->body = n->a;
		setstatus("");
		return;
	}
	case N_MATCH: {
		Word *subj = eval1(n->a);
		setstatus(match_any(subj, n->a->next) ? "" : "no match");
		freewords(subj);
		return;
	}
	case N_BG: {
		int pid;
		char b[16];
		fflush(stdout);
		pid = fork();
		if (pid == 0) {
			run(n->a);
			fflush(stdout);
			_exit(exit_code());
		}
		sprintf(b, "%d", pid);
		setvar1("apid", b);
		setstatus("");
		return;
	}
	}
}

static void run_list(Node *n) {
	for (; n && !exiting; n = n->next) run(n);
}

static int run_text(char *text, int checkonly) {
	char *osrc = src;
	int opos = pos, olen = srclen, on = npushed;
	Tok otok = tok, op[4];
	memcpy(op, pushed, sizeof(pushed));
	src = text;
	pos = 0;
	srclen = (int)strlen(text);
	npushed = 0;
	incomplete = 0;
	quiet = checkonly;
	for (;;) {
		Node *c;
		syntax_error = 0;
		next();
		if (tok.type == T_EOF) break;
		if (tok.type == T_NL || tok.type == T_SEMI) continue;
		unread();
		c = parse_andor();
		if (!syntax_error) {
			next();
			if (tok.type == T_AMP) {
				Node *b = node(N_BG);
				b->a = c;
				c = b;
			} else if (tok.type != T_NL && tok.type != T_SEMI && tok.type != T_EOF) synerr("unexpected token");
			else unread();
		}
		if (syntax_error) {
			if (checkonly) break;
			while (src[pos] && src[pos] != '\n') pos++;
			npushed = 0;
			setstatus("syntax error");
			if (!interactive) {
				exiting = 1;
				break;
			}
			continue;
		}
		if (!checkonly) run(c);
		if (exiting) break;
	}
	quiet = 0;
	src = osrc;
	pos = opos;
	srclen = olen;
	npushed = on;
	tok = otok;
	memcpy(pushed, op, sizeof(pushed));
	return incomplete;
}

static char *read_all(int fd) {
	int cap = 4096, len = 0, n;
	char *buf = emalloc((unsigned)cap);
	while ((n = read(fd, buf + len, (unsigned)(cap - len - 1))) > 0) {
		len += n;
		if (len + 1 >= cap) {
			cap *= 2;
			buf = realloc(buf, (unsigned)cap);
		}
	}
	buf[len] = 0;
	return buf;
}

static void prompt(int second) {
	Word *p = getvar("prompt");
	const char *s = p ? (second && p->next ? p->next->s : second ? "\t" : p->s) : (second ? "\t" : "% ");
	fputs(s, stderr);
	freewords(p);
}

static void interact(void) {
	int cap = 1024, len = 0;
	char *buf = emalloc((unsigned)cap), line[1024];
	for (;;) {
		int n, k = 0;
		prompt(len > 0);
		for (;;) {
			char c;
			n = read(0, &c, 1);
			if (n <= 0) break;
			if (k < (int)sizeof(line) - 2) line[k++] = c;
			if (c == '\n') break;
		}
		if (n <= 0 && k == 0) break;
		line[k] = 0;
		if (len + k + 1 >= cap) {
			cap = (len + k + 1) * 2;
			buf = realloc(buf, (unsigned)cap);
		}
		strcpy(buf + len, line);
		len += k;
		syntax_error = 0;
		if (run_text(buf, 1)) continue;
		run_text(buf, 0);
		len = 0;
		buf[0] = 0;
		if (exiting) break;
	}
}

int main(int argc, char **argv) {
	int i = 1;
	char *cmd = NULL, buf[16];
	Word *w;
	signal(SIGPIPE, SIG_DFL);
	import_env(environ);
	while (i < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-c") && i + 1 < argc) {
			cmd = argv[i + 1];
			i += 2;
		} else if (!strcmp(argv[i], "-i")) {
			interactive = 1;
			i++;
		} else break;
	}
	if (!findvar("path")) {
		w = getvar("PATH");
		if (w) {
			Word *h = NULL, **t = &h;
			char *s = w->s;
			for (;;) {
				char *e = strchr(s, ':');
				if (e) *e = 0;
				*t = word(*s ? s : ".", NULL);
				t = &(*t)->next;
				if (!e) break;
				s = e + 1;
			}
			setvar("path", h);
			freewords(w);
		} else setvar("path", word(".", word("/bin", NULL)));
	}
	if (!findvar("home") && (w = getvar("HOME")) != NULL) setvar("home", w);
	if (!findvar("user") && (w = getvar("USER")) != NULL) setvar("user", w);
	if (!findvar("prompt")) {
		Var *v = emalloc(sizeof(Var));
		v->name = estrdup("prompt");
		v->val = word("% ", word("\t", NULL));
		v->next = vars;
		vars = v;
	}
	sprintf(buf, "%d", getpid());
	setvar1("pid", buf);
	setstatus("");
	if (cmd) {
		setvar("*", NULL);
		for (; i < argc; i++) setvar("*", append(getvar("*"), word(argv[i], NULL)));
		run_text(cmd, 0);
		return exit_code();
	}
	if (i < argc) {
		int fd = open(argv[i], O_RDONLY);
		char *text;
		if (fd < 0) {
			fprintf(stderr, "rc: %s: %s\n", argv[i], strerror(errno));
			return 1;
		}
		cmdname = argv[i];
		text = read_all(fd);
		close(fd);
		setvar("*", NULL);
		for (i++; i < argc; i++) setvar("*", append(getvar("*"), word(argv[i], NULL)));
		run_text(text, 0);
		return exit_code();
	}
	if (!interactive) interactive = isatty(0);
	if (interactive) {
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		interact();
	} else run_text(read_all(0), 0);
	return exit_code();
}
