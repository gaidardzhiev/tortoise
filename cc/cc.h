/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef CC_H
#define CC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

enum {
	T_EOF, T_IDENT, T_NUM, T_CHAR, T_STR, T_PUNCT
};

typedef struct Tok Tok;
struct Tok {
	int kind;
	char *text;
	unsigned val;
	int uns;
	char *str;
	int len;
	char *file;
	int line;
	int bol;
	int space;
	int noexp;
	Tok *next;
};

enum {
	TY_VOID, TY_CHAR, TY_SHORT, TY_INT, TY_PTR, TY_ARRAY, TY_STRUCT, TY_UNION, TY_FUNC, TY_ENUM
};

typedef struct Type Type;
typedef struct Member Member;
typedef struct Param Param;

struct Member {
	char *name;
	Type *ty;
	int off;
	Member *next;
};

struct Param {
	char *name;
	Type *ty;
	Param *next;
};

struct Type {
	int kind;
	int uns;
	int size;
	int align;
	Type *base;
	int len;
	Member *members;
	int complete;
	Param *params;
	int variadic;
	int proto;
	char *tag;
	Type *origin;
};

enum {
	S_LOCAL, S_GLOBAL, S_FUNC, S_TYPEDEF, S_ENUM
};

typedef struct Sym Sym;
struct Sym {
	char *name;
	int kind;
	Type *ty;
	int off;
	char *label;
	int is_static;
	int is_extern;
	int defined;
	int tentative;
	int val;
	int depth;
	Sym *next;
	Sym *hnext;
};

enum {
	N_NUM, N_VAR, N_STR, N_ADD, N_SUB, N_MUL, N_DIV, N_MOD, N_AND, N_OR, N_XOR, N_SHL, N_SHR,
	N_EQ, N_NE, N_LT, N_LE, N_GT, N_GE, N_LOGAND, N_LOGOR, N_NOT, N_BITNOT, N_NEG, N_ADDR,
	N_DEREF, N_ASSIGN, N_OPASSIGN, N_PREINC, N_PREDEC, N_POSTINC, N_POSTDEC, N_COND, N_COMMA,
	N_CALL, N_CAST, N_VASTART, N_VAARG, N_MEMZERO,
	N_IF, N_WHILE, N_DO, N_FOR, N_SWITCH, N_CASE, N_DEFAULT, N_BREAK, N_CONTINUE, N_RETURN,
	N_GOTO, N_LABEL, N_BLOCK, N_EXPR, N_NULL
};

typedef struct Node Node;
struct Node {
	int kind;
	Type *ty;
	Node *l;
	Node *r;
	Node *c;
	Node *body;
	Node *init;
	Node *inc;
	Node *args;
	Node *next;
	Node *cases;
	Node *cnext;
	Node *deflt;
	int op;
	unsigned val;
	Sym *sym;
	char *label;
	int lab;
	int brk;
	int cont;
	int tmpoff;
	int size;
	Tok *tok;
};

typedef struct Func Func;
struct Func {
	Sym *sym;
	Node *body;
	Param *params;
	Sym *pvars;
	int frame;
	int sret;
	Func *next;
};

typedef struct Init Init;
struct Init {
	int off;
	int size;
	unsigned val;
	char *label;
	Init *next;
};

typedef struct GVar GVar;
struct GVar {
	Sym *sym;
	Init *inits;
	int has_init;
	GVar *next;
};

typedef struct StrLit StrLit;
struct StrLit {
	char *label;
	char *data;
	int len;
	StrLit *next;
};

extern char *incdirs[32];
extern int nincdirs;
extern FILE *outf;
extern Func *funcs;
extern GVar *gvars;
extern StrLit *strlits;
extern Type *ty_void, *ty_char, *ty_uchar, *ty_short, *ty_ushort, *ty_int, *ty_uint;

void *xmalloc(int n);
char *xstrdup(const char *s);
char *fmt(const char *f, ...);
void error_at(Tok *t, const char *f, ...);
void fatal(const char *f, ...);

void pp_define(const char *def);
Tok *preprocess(const char *path);

Type *new_type(int kind, int size, int align);
Type *ptr_to(Type *base);
Type *array_of(Type *base, int len);
Type *func_type(Type *ret);
int is_integer(Type *t);
int is_ptr(Type *t);
int is_scalar(Type *t);
void parse_program(Tok *toks);

void gen_program(void);
int new_label(void);

#endif
