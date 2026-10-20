#include "check.h"
#include <string.h>
#include <ctype.h>

enum { TK_NUM, TK_ID, TK_OP, TK_END };
enum { A_NUM, A_VAR, A_BIN, A_NEG, A_NOT, A_CALL, A_ASSIGN, A_IF, A_WHILE, A_BLOCK, A_PRINT, A_RET, A_FUNC };

typedef struct Ast Ast;
struct Ast {
	int kind;
	int val;
	char name[16];
	char op[3];
	Ast *a, *b, *c;
	Ast *next;
	char params[4][16];
	int nparams;
};

typedef struct {
	char name[16];
	int val;
} Var;

typedef struct {
	char name[16];
	Ast *def;
} FuncDef;

static const char *src;
static int tk;
static int tkval;
static char tktext[16];
static char out[1024];
static int outlen;
static FuncDef funcs[16];
static int nfuncs;
static int returning;
static int retval;
static int errors;

static void next(void) {
	while (isspace(*src)) src++;
	if (!*src) {
		tk = TK_END;
		return;
	}
	if (isdigit(*src)) {
		tkval = 0;
		while (isdigit(*src)) tkval = tkval * 10 + *src++ - '0';
		tk = TK_NUM;
		return;
	}
	if (isalpha(*src)) {
		int n = 0;
		while (isalnum(*src) && n < 15) tktext[n++] = *src++;
		tktext[n] = 0;
		tk = TK_ID;
		return;
	}
	tktext[0] = *src++;
	tktext[1] = 0;
	if ((tktext[0] == '=' || tktext[0] == '!' || tktext[0] == '<' || tktext[0] == '>') && *src == '=') {
		tktext[1] = *src++;
		tktext[2] = 0;
	} else if ((tktext[0] == '&' && *src == '&') || (tktext[0] == '|' && *src == '|')) {
		tktext[1] = *src++;
		tktext[2] = 0;
	}
	tk = TK_OP;
}

static int is_op(const char *s) {
	return tk == TK_OP && strcmp(tktext, s) == 0;
}

static int is_kw(const char *s) {
	return tk == TK_ID && strcmp(tktext, s) == 0;
}

static void expect(const char *s) {
	if (!is_op(s)) {
		errors++;
		printf("parse error: expected %s got %s\n", s, tktext);
	}
	next();
}

static Ast *node(int kind) {
	Ast *n = calloc(1, sizeof(Ast));
	n->kind = kind;
	return n;
}

static Ast *expr(void);

static Ast *primary(void) {
	Ast *n;
	if (tk == TK_NUM) {
		n = node(A_NUM);
		n->val = tkval;
		next();
		return n;
	}
	if (tk == TK_ID) {
		char name[16];
		strcpy(name, tktext);
		next();
		if (is_op("(")) {
			Ast head, *cur = &head;
			n = node(A_CALL);
			strcpy(n->name, name);
			next();
			head.next = NULL;
			while (!is_op(")")) {
				cur->next = expr();
				cur = cur->next;
				if (is_op(",")) next();
			}
			next();
			n->a = head.next;
			return n;
		}
		n = node(A_VAR);
		strcpy(n->name, name);
		return n;
	}
	if (is_op("(")) {
		next();
		n = expr();
		expect(")");
		return n;
	}
	if (is_op("-")) {
		next();
		n = node(A_NEG);
		n->a = primary();
		return n;
	}
	if (is_op("!")) {
		next();
		n = node(A_NOT);
		n->a = primary();
		return n;
	}
	errors++;
	printf("parse error at %s\n", tktext);
	next();
	return node(A_NUM);
}

static int prec(void) {
	static const char *ops[] = {"||", "&&", "==", "!=", "<", "<=", ">", ">=", "+", "-", "*", "/", "%", NULL};
	static const int p[] = {1, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6};
	int i;
	if (tk != TK_OP) return 0;
	for (i = 0; ops[i]; i++)
		if (strcmp(tktext, ops[i]) == 0) return p[i];
	return 0;
}

static Ast *binary(int minp) {
	Ast *l = primary();
	while (prec() >= minp && prec() > 0) {
		int p = prec();
		Ast *n = node(A_BIN);
		strcpy(n->op, tktext);
		next();
		n->a = l;
		n->b = binary(p + 1);
		l = n;
	}
	return l;
}

static Ast *expr(void) {
	return binary(1);
}

static Ast *statement(void);

static Ast *block(void) {
	Ast *n = node(A_BLOCK), head, *cur = &head;
	head.next = NULL;
	expect("{");
	while (!is_op("}") && tk != TK_END) {
		cur->next = statement();
		cur = cur->next;
	}
	expect("}");
	n->a = head.next;
	return n;
}

static Ast *statement(void) {
	Ast *n;
	if (is_kw("print")) {
		next();
		n = node(A_PRINT);
		n->a = expr();
		expect(";");
		return n;
	}
	if (is_kw("if")) {
		next();
		n = node(A_IF);
		n->a = expr();
		n->b = block();
		if (is_kw("else")) {
			next();
			n->c = is_kw("if") ? statement() : block();
		}
		return n;
	}
	if (is_kw("while")) {
		next();
		n = node(A_WHILE);
		n->a = expr();
		n->b = block();
		return n;
	}
	if (is_kw("return")) {
		next();
		n = node(A_RET);
		n->a = expr();
		expect(";");
		return n;
	}
	if (is_kw("def")) {
		next();
		n = node(A_FUNC);
		strcpy(n->name, tktext);
		next();
		expect("(");
		while (!is_op(")")) {
			strcpy(n->params[n->nparams++], tktext);
			next();
			if (is_op(",")) next();
		}
		next();
		n->a = block();
		return n;
	}
	if (tk == TK_ID) {
		const char *save = src;
		char name[16];
		strcpy(name, tktext);
		next();
		if (is_op("=")) {
			next();
			n = node(A_ASSIGN);
			strcpy(n->name, name);
			n->a = expr();
			expect(";");
			return n;
		}
		src = save;
		strcpy(tktext, name);
		tk = TK_ID;
	}
	n = expr();
	expect(";");
	return n;
}

typedef struct {
	Var vars[32];
	int n;
} Env;

static int *lookup(Env *e, const char *name) {
	int i;
	for (i = 0; i < e->n; i++)
		if (strcmp(e->vars[i].name, name) == 0) return &e->vars[i].val;
	if (e->n >= 32) return NULL;
	strcpy(e->vars[e->n].name, name);
	e->vars[e->n].val = 0;
	return &e->vars[e->n++].val;
}

static void emitnum(int v) {
	outlen += sprintf(out + outlen, "%d ", v);
}

static int eval(Ast *n, Env *e);

static void exec(Ast *n, Env *e) {
	Ast *s;
	if (returning) return;
	switch (n->kind) {
	case A_BLOCK:
		for (s = n->a; s && !returning; s = s->next) exec(s, e);
		break;
	case A_PRINT:
		emitnum(eval(n->a, e));
		break;
	case A_ASSIGN:
		*lookup(e, n->name) = eval(n->a, e);
		break;
	case A_IF:
		if (eval(n->a, e)) exec(n->b, e);
		else if (n->c) exec(n->c, e);
		break;
	case A_WHILE:
		while (!returning && eval(n->a, e)) exec(n->b, e);
		break;
	case A_RET:
		retval = eval(n->a, e);
		returning = 1;
		break;
	case A_FUNC:
		strcpy(funcs[nfuncs].name, n->name);
		funcs[nfuncs++].def = n;
		break;
	default:
		eval(n, e);
	}
}

static int call(const char *name, int *args, int nargs) {
	int i;
	for (i = 0; i < nfuncs; i++) {
		if (strcmp(funcs[i].name, name) == 0) {
			Env local;
			Ast *f = funcs[i].def;
			int k, r;
			local.n = 0;
			for (k = 0; k < f->nparams && k < nargs; k++) *lookup(&local, f->params[k]) = args[k];
			exec(f->a, &local);
			r = returning ? retval : 0;
			returning = 0;
			return r;
		}
	}
	errors++;
	printf("unknown function %s\n", name);
	return 0;
}

static int eval(Ast *n, Env *e) {
	int a, b;
	switch (n->kind) {
	case A_NUM: return n->val;
	case A_VAR: return *lookup(e, n->name);
	case A_NEG: return -eval(n->a, e);
	case A_NOT: return !eval(n->a, e);
	case A_CALL: {
		int args[8], k = 0;
		Ast *x;
		for (x = n->a; x && k < 8; x = x->next) args[k++] = eval(x, e);
		return call(n->name, args, k);
	}
	case A_BIN:
		if (strcmp(n->op, "&&") == 0) return eval(n->a, e) && eval(n->b, e);
		if (strcmp(n->op, "||") == 0) return eval(n->a, e) || eval(n->b, e);
		a = eval(n->a, e);
		b = eval(n->b, e);
		switch (n->op[0]) {
		case '+': return a + b;
		case '-': return a - b;
		case '*': return a * b;
		case '/': return b ? a / b : 0;
		case '%': return b ? a % b : 0;
		case '<': return n->op[1] ? a <= b : a < b;
		case '>': return n->op[1] ? a >= b : a > b;
		case '=': return a == b;
		case '!': return a != b;
		}
	}
	errors++;
	return 0;
}

static const char *run(const char *program) {
	Env global;
	Ast head, *cur = &head, *s;
	global.n = 0;
	outlen = 0;
	out[0] = 0;
	nfuncs = 0;
	returning = 0;
	src = program;
	next();
	head.next = NULL;
	while (tk != TK_END) {
		cur->next = statement();
		cur = cur->next;
	}
	for (s = head.next; s; s = s->next) exec(s, &global);
	return out;
}

int main(void) {
	CHECK(strcmp(run("print 1 + 2 * 3; print (1 + 2) * 3; print 10 / 3; print 10 % 3; print -5 + 2;"), "7 9 3 1 -3 ") == 0);
	CHECK(strcmp(run("x = 5; y = x * x; print y; x = x - 1; print x;"), "25 4 ") == 0);
	CHECK(strcmp(run("i = 0; s = 0; while i < 10 { s = s + i; i = i + 1; } print s;"), "45 ") == 0);
	CHECK(strcmp(run("if 1 < 2 { print 1; } else { print 2; } if 3 <= 2 { print 3; } else if 0 { print 4; } else { print 5; }"), "1 5 ") == 0);
	CHECK(strcmp(run("def sq(x) { return x * x; } print sq(7); print sq(sq(2));"), "49 16 ") == 0);
	CHECK(strcmp(run("def fib(n) { if n < 2 { return n; } return fib(n - 1) + fib(n - 2); } print fib(15);"), "610 ") == 0);
	CHECK(strcmp(run("def fact(n) { r = 1; while n > 1 { r = r * n; n = n - 1; } return r; } print fact(10);"), "3628800 ") == 0);
	CHECK(strcmp(run("print 1 && 0; print 1 || 0; print !0; print 3 == 3; print 3 != 3; print 2 >= 3;"), "0 1 1 1 0 0 ") == 0);
	CHECK(strcmp(run("def gcd(a, b) { while b != 0 { t = b; b = a % b; a = t; } return a; } print gcd(1071, 462);"), "21 ") == 0);
	CHECK(strcmp(run("n = 0; i = 2; while i < 100 { p = 1; j = 2; while j * j <= i { if i % j == 0 { p = 0; } j = j + 1; } n = n + p; i = i + 1; } print n;"), "25 ") == 0);
	CHECK(strcmp(run("def ack(m, n) { if m == 0 { return n + 1; } if n == 0 { return ack(m - 1, 1); } return ack(m - 1, ack(m, n - 1)); } print ack(2, 2);"), "7 ") == 0);
	CHECK(errors == 0);
	DONE();
}
