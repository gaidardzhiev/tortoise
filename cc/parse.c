/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "cc.h"

#define GHASH 2048

typedef struct Tag Tag;
struct Tag {
	char *name;
	Type *ty;
	int depth;
	Tag *next;
};

typedef struct LInit LInit;
struct LInit {
	int off;
	Type *ty;
	Node *expr;
	LInit *next;
};

typedef struct LabelRef LabelRef;
struct LabelRef {
	char *name;
	int lab;
	int defined;
	Tok *tok;
	LabelRef *next;
};

Func *funcs;
GVar *gvars;
StrLit *strlits;

static Type t_void = {TY_VOID, 0, 1, 1, NULL, 0, NULL, 1, NULL, 0, 0, NULL, NULL};
static Type t_char = {TY_CHAR, 0, 1, 1, NULL, 0, NULL, 1, NULL, 0, 0, NULL, NULL};
static Type t_uchar = {TY_CHAR, 1, 1, 1, NULL, 0, NULL, 1, NULL, 0, 0, NULL, NULL};
static Type t_short = {TY_SHORT, 0, 2, 2, NULL, 0, NULL, 1, NULL, 0, 0, NULL, NULL};
static Type t_ushort = {TY_SHORT, 1, 2, 2, NULL, 0, NULL, 1, NULL, 0, 0, NULL, NULL};
static Type t_int = {TY_INT, 0, 4, 4, NULL, 0, NULL, 1, NULL, 0, 0, NULL, NULL};
static Type t_uint = {TY_INT, 1, 4, 4, NULL, 0, NULL, 1, NULL, 0, 0, NULL, NULL};

Type *ty_void = &t_void;
Type *ty_char = &t_char;
Type *ty_uchar = &t_uchar;
Type *ty_short = &t_short;
Type *ty_ushort = &t_ushort;
Type *ty_int = &t_int;
Type *ty_uint = &t_uint;

static Tok *tok;
static Sym *gtab[GHASH];
static Sym *locals;
static Tag *tags;
static int depth;
static int cur_off;
static int max_off;
static Func *cur_func;
static Node *cur_switch;
static int brk_lab, cont_lab;
static LabelRef *labels;
static int nstatic;
static int nstr;
static Func *func_tail;
static GVar *gvar_tail;

static Node *expr(void);
static Node *assign(void);
static Node *cast_expr(void);
static Node *cond_expr(void);
static Node *stmt(void);
static Node *compound(void);
static Type *declspec(int *sclass);
static Type *declarator(Type *ty, char **name, Tok **ntok);
static Type *abstract_declarator(Type *ty);
static int is_typename(Tok *t);
static unsigned const_expr(void);

static int equal(Tok *t, const char *s) {
	return (t->kind == T_PUNCT || t->kind == T_IDENT) && strcmp(t->text, s) == 0;
}

static int consume(const char *s) {
	if (equal(tok, s)) {
		tok = tok->next;
		return 1;
	}
	return 0;
}

static void expect(const char *s) {
	if (!consume(s)) error_at(tok, "expected '%s' before '%s'", s, tok->kind == T_EOF ? "end of file" : tok->text);
}

static char *expect_ident(void) {
	char *s;
	if (tok->kind != T_IDENT) error_at(tok, "identifier expected before '%s'", tok->text);
	s = tok->text;
	tok = tok->next;
	return s;
}

Type *new_type(int kind, int size, int align) {
	Type *t = xmalloc(sizeof(Type));
	t->kind = kind;
	t->size = size;
	t->align = align;
	t->complete = 1;
	return t;
}

Type *ptr_to(Type *base) {
	Type *t = new_type(TY_PTR, 4, 4);
	t->base = base;
	t->uns = 1;
	return t;
}

Type *array_of(Type *base, int len) {
	Type *t = new_type(TY_ARRAY, len < 0 ? 0 : base->size * len, base->align);
	t->base = base;
	t->len = len;
	if (len < 0) t->complete = 0;
	return t;
}

Type *func_type(Type *ret) {
	Type *t = new_type(TY_FUNC, 1, 1);
	t->base = ret;
	return t;
}

int is_integer(Type *t) {
	return t->kind == TY_CHAR || t->kind == TY_SHORT || t->kind == TY_INT || t->kind == TY_ENUM;
}

int is_ptr(Type *t) {
	return t->kind == TY_PTR || t->kind == TY_ARRAY;
}

int is_scalar(Type *t) {
	return is_integer(t) || t->kind == TY_PTR;
}

static int is_aggr(Type *t) {
	return t->kind == TY_STRUCT || t->kind == TY_UNION || t->kind == TY_ARRAY;
}

static Type *vtype(Type *t) {
	if (t->kind == TY_ARRAY) return ptr_to(t->base);
	if (t->kind == TY_FUNC) return ptr_to(t);
	return t;
}

static Type *promote(Type *t) {
	if (t->kind == TY_CHAR || t->kind == TY_SHORT || t->kind == TY_ENUM) return ty_int;
	return t;
}

static Type *arith_conv(Type *a, Type *b) {
	a = promote(a);
	b = promote(b);
	if (a->uns || b->uns) return ty_uint;
	return ty_int;
}

static unsigned hashs(const char *s) {
	unsigned h = 5381;
	while (*s) h = h * 33 + (unsigned char)*s++;
	return h % GHASH;
}

static Sym *find_sym(const char *name) {
	Sym *s;
	for (s = locals; s; s = s->next)
		if (strcmp(s->name, name) == 0) return s;
	for (s = gtab[hashs(name)]; s; s = s->hnext)
		if (strcmp(s->name, name) == 0) return s;
	return NULL;
}

static Sym *new_sym(const char *name, int kind, Type *ty) {
	Sym *s = xmalloc(sizeof(Sym));
	s->name = (char *)name;
	s->kind = kind;
	s->ty = ty;
	s->depth = depth;
	return s;
}

static Sym *add_global(const char *name, int kind, Type *ty) {
	Sym *s = new_sym(name, kind, ty);
	unsigned h = hashs(name);
	s->hnext = gtab[h];
	gtab[h] = s;
	s->label = (char *)name;
	return s;
}

static Sym *find_global(const char *name) {
	Sym *s;
	for (s = gtab[hashs(name)]; s; s = s->hnext)
		if (strcmp(s->name, name) == 0) return s;
	return NULL;
}

static void check_redecl_local(const char *name, Tok *t) {
	Sym *s;
	for (s = locals; s && s->depth == depth; s = s->next)
		if (strcmp(s->name, name) == 0) error_at(t, "redeclaration of '%s'", name);
}

static Sym *add_local(const char *name, int kind, Type *ty) {
	Sym *s = new_sym(name, kind, ty);
	s->next = locals;
	locals = s;
	return s;
}

static int alloc_local(Type *ty) {
	int al = ty->align < 4 ? 4 : ty->align;
	int sz = (ty->size + 3) & ~3;
	cur_off += sz;
	cur_off = (cur_off + al - 1) & ~(al - 1);
	if (cur_off > max_off) max_off = cur_off;
	return -cur_off;
}

static void enter_scope(void) {
	depth++;
}

static void leave_scope(int save_off) {
	while (locals && locals->depth == depth) locals = locals->next;
	while (tags && tags->depth == depth) tags = tags->next;
	depth--;
	cur_off = save_off;
}

static Tag *find_tag(const char *name) {
	Tag *t;
	for (t = tags; t; t = t->next)
		if (strcmp(t->name, name) == 0) return t;
	return NULL;
}

static Node *new_node(int kind, Tok *t) {
	Node *n = xmalloc(sizeof(Node));
	n->kind = kind;
	n->tok = t;
	return n;
}

static Node *new_num(unsigned v, Type *ty, Tok *t) {
	Node *n = new_node(N_NUM, t);
	n->val = v;
	n->ty = ty;
	return n;
}

static Node *new_cast(Node *e, Type *ty) {
	Node *n;
	if (e->ty == ty) return e;
	n = new_node(N_CAST, e->tok);
	n->l = e;
	n->ty = ty;
	return n;
}

static int is_lvalue(Node *n) {
	return n->kind == N_VAR || n->kind == N_DEREF || n->kind == N_STR;
}

static void need_lvalue(Node *n) {
	if (!is_lvalue(n)) error_at(n->tok, "lvalue required");
	if (n->ty->kind == TY_ARRAY || n->ty->kind == TY_FUNC) error_at(n->tok, "assignment to an array or function");
}

static Node *new_binary(int kind, Node *l, Node *r, Tok *t);

static Node *scale(Node *n, int size, Tok *t) {
	if (size == 1) return n;
	return new_binary(N_MUL, n, new_num((unsigned)size, ty_int, t), t);
}

static int sizeof_base(Type *t, Tok *at) {
	Type *b = t->base;
	if (b->kind == TY_VOID) return 1;
	if (b->kind == TY_FUNC) return 1;
	if (!b->complete && b->kind != TY_ARRAY) error_at(at, "arithmetic on pointer to incomplete type");
	return b->size ? b->size : 1;
}

static Node *new_binary(int kind, Node *l, Node *r, Tok *t) {
	Node *n = new_node(kind, t);
	Type *lt = vtype(l->ty), *rt = vtype(r->ty);
	n->l = l;
	n->r = r;
	switch (kind) {
	case N_ADD:
		if (is_integer(lt) && is_integer(rt)) {
			n->ty = arith_conv(lt, rt);
		} else if (lt->kind == TY_PTR && is_integer(rt)) {
			n->r = scale(r, sizeof_base(lt, t), t);
			n->ty = lt;
		} else if (is_integer(lt) && rt->kind == TY_PTR) {
			n->l = r;
			n->r = scale(l, sizeof_base(rt, t), t);
			n->ty = rt;
		} else {
			error_at(t, "invalid operands to '+'");
		}
		break;
	case N_SUB:
		if (is_integer(lt) && is_integer(rt)) {
			n->ty = arith_conv(lt, rt);
		} else if (lt->kind == TY_PTR && is_integer(rt)) {
			n->r = scale(r, sizeof_base(lt, t), t);
			n->ty = lt;
		} else if (lt->kind == TY_PTR && rt->kind == TY_PTR) {
			int sz = sizeof_base(lt, t);
			n->ty = ty_int;
			if (sz != 1) {
				Node *d = new_node(N_DIV, t);
				d->l = n;
				d->r = new_num((unsigned)sz, ty_int, t);
				d->ty = ty_int;
				return d;
			}
		} else {
			error_at(t, "invalid operands to '-'");
		}
		break;
	case N_MUL: case N_DIV: case N_MOD: case N_AND: case N_OR: case N_XOR:
		if (!is_integer(lt) || !is_integer(rt)) error_at(t, "invalid operands to binary operator");
		n->ty = arith_conv(lt, rt);
		break;
	case N_SHL: case N_SHR:
		if (!is_integer(lt) || !is_integer(rt)) error_at(t, "invalid operands to shift");
		n->ty = promote(lt);
		break;
	case N_EQ: case N_NE: case N_LT: case N_LE: case N_GT: case N_GE:
		if (!is_scalar(lt) || !is_scalar(rt)) error_at(t, "invalid operands to comparison");
		n->ty = ty_int;
		if (lt->kind == TY_PTR || rt->kind == TY_PTR) n->op = 1;
		else n->op = arith_conv(lt, rt)->uns;
		break;
	case N_LOGAND: case N_LOGOR:
		if (!is_scalar(lt) || !is_scalar(rt)) error_at(t, "invalid operands to logical operator");
		n->ty = ty_int;
		break;
	case N_COMMA:
		n->ty = r->ty;
		break;
	default:
		error_at(t, "internal error: bad binary operator");
	}
	return n;
}

static Node *new_unary(int kind, Node *l, Tok *t) {
	Node *n = new_node(kind, t);
	Type *lt = vtype(l->ty);
	n->l = l;
	switch (kind) {
	case N_NEG: case N_BITNOT:
		if (!is_integer(lt)) error_at(t, "invalid operand to unary operator");
		n->ty = promote(lt);
		break;
	case N_NOT:
		if (!is_scalar(lt)) error_at(t, "invalid operand to '!'");
		n->ty = ty_int;
		break;
	case N_ADDR:
		if (l->kind == N_VAR && l->ty->kind == TY_FUNC) {
			n->ty = ptr_to(l->ty);
			break;
		}
		if (!is_lvalue(l) && !((l->ty->kind == TY_STRUCT || l->ty->kind == TY_UNION) && (l->kind == N_CALL || l->kind == N_ASSIGN || l->kind == N_COND || l->kind == N_COMMA || l->kind == N_VAARG))) error_at(t, "cannot take the address of an rvalue");
		n->ty = ptr_to(l->ty);
		break;
	case N_DEREF:
		if (lt->kind != TY_PTR) error_at(t, "dereference of a non-pointer");
		if (lt->base->kind == TY_VOID) error_at(t, "dereference of a void pointer");
		if (lt->base->kind == TY_FUNC) return l;
		n->ty = lt->base;
		break;
	default:
		error_at(t, "internal error: bad unary operator");
	}
	return n;
}

static Node *conv_assign(Node *r, Type *to, Tok *t) {
	Type *rt = vtype(r->ty);
	if (to->kind == TY_STRUCT || to->kind == TY_UNION) {
		if (rt != to && !(rt->kind == to->kind && rt->size == to->size && rt->members == to->members))
			error_at(t, "incompatible struct or union types");
		return r;
	}
	if (to->kind == TY_VOID) error_at(t, "void value used");
	if (!is_scalar(rt)) error_at(t, "incompatible types in assignment");
	if (is_scalar(to)) return new_cast(r, to);
	error_at(t, "incompatible types in assignment");
	return r;
}

static Node *new_assign(Node *l, Node *r, Tok *t) {
	Node *n = new_node(N_ASSIGN, t);
	need_lvalue(l);
	n->l = l;
	n->r = conv_assign(r, l->ty, t);
	n->ty = l->ty;
	return n;
}

static Node *member_ref(Node *base, Tok *t) {
	char *name;
	Member *m;
	Type *st = base->ty;
	Node *addr, *n;
	if (st->kind != TY_STRUCT && st->kind != TY_UNION) error_at(t, "member access of a non-struct");
	if (!st->complete) error_at(t, "use of incomplete struct");
	name = expect_ident();
	for (m = st->members; m; m = m->next)
		if (m->name && strcmp(m->name, name) == 0) break;
	if (!m) error_at(t, "no member named '%s'", name);
	if (base->kind == N_DEREF) addr = base->l;
	else addr = new_unary(N_ADDR, base, t);
	n = new_node(N_ADD, t);
	n->l = addr;
	n->r = new_num((unsigned)m->off, ty_int, t);
	n->ty = ptr_to(m->ty);
	addr = new_node(N_DEREF, t);
	addr->l = n;
	addr->ty = m->ty;
	return addr;
}

static StrLit *add_string(char *data, int len) {
	StrLit *s = xmalloc(sizeof(StrLit));
	s->label = fmt("_Str%d", nstr++);
	s->data = data;
	s->len = len;
	s->next = strlits;
	strlits = s;
	return s;
}

static Node *string_lit(void) {
	Tok *start = tok;
	int len = 0;
	char *buf;
	Tok *t;
	Node *n;
	StrLit *s;
	for (t = tok; t->kind == T_STR; t = t->next) len += t->len;
	buf = xmalloc(len + 1);
	len = 0;
	for (; tok->kind == T_STR; tok = tok->next) {
		memcpy(buf + len, tok->str, tok->len);
		len += tok->len;
	}
	buf[len] = 0;
	s = add_string(buf, len);
	n = new_node(N_STR, start);
	n->label = s->label;
	n->ty = array_of(ty_char, len + 1);
	n->val = (unsigned)len;
	return n;
}

static Node *funcall(Node *fn, Tok *t) {
	Node *n = new_node(N_CALL, t);
	Type *ft = vtype(fn->ty);
	Param *p;
	Node head, *cur = &head;
	int argn = 0;
	if (ft->kind == TY_PTR && ft->base->kind == TY_FUNC) ft = ft->base;
	if (ft->kind != TY_FUNC) error_at(t, "called object is not a function");
	head.next = NULL;
	p = ft->params;
	if (!equal(tok, ")")) {
		do {
			Node *a = assign();
			if (vtype(a->ty)->kind == TY_VOID) error_at(a->tok, "void value used as argument");
			if (p) {
				a = conv_assign(a, p->ty, a->tok);
				p = p->next;
			} else {
				if (ft->proto && !ft->variadic) error_at(a->tok, "too many arguments in function call");
				if (is_integer(vtype(a->ty))) a = new_cast(a, promote(vtype(a->ty)));
			}
			cur->next = a;
			cur = a;
			argn++;
		} while (consume(","));
	}
	expect(")");
	if (p && ft->proto) error_at(t, "too few arguments in function call");
	n->l = fn;
	n->args = head.next;
	n->ty = ft->base;
	if (n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION) n->tmpoff = alloc_local(n->ty);
	return n;
}

static Node *primary(void) {
	Tok *t = tok;
	if (consume("(")) {
		Node *n = expr();
		expect(")");
		return n;
	}
	if (t->kind == T_NUM || t->kind == T_CHAR) {
		tok = t->next;
		return new_num(t->val, t->uns ? ty_uint : ty_int, t);
	}
	if (t->kind == T_STR) return string_lit();
	if (t->kind == T_IDENT) {
		Sym *s;
		Node *n;
		if (equal(t, "__builtin_va_start")) {
			tok = t->next;
			expect("(");
			n = new_node(N_VASTART, t);
			n->l = assign();
			expect(",");
			n->r = assign();
			expect(")");
			if (n->r->kind != N_VAR || n->r->sym->kind != S_LOCAL) error_at(t, "va_start needs the last named parameter");
			n->ty = ty_void;
			return n;
		}
		if (equal(t, "__builtin_va_arg")) {
			Type *ty;
			int sc = 0;
			tok = t->next;
			expect("(");
			n = new_node(N_VAARG, t);
			n->l = assign();
			expect(",");
			ty = declspec(&sc);
			ty = abstract_declarator(ty);
			expect(")");
			n->ty = ty;
			n->size = (ty->size + 3) & ~3;
			return n;
		}
		if (equal(t, "__builtin_va_end")) {
			tok = t->next;
			expect("(");
			assign();
			expect(")");
			return new_num(0, ty_void, t);
		}
		tok = t->next;
		s = find_sym(t->text);
		if (!s) {
			if (equal(tok, "(")) {
				fprintf(stderr, "%s:%d: warning: implicit declaration of function '%s'\n", t->file, t->line, t->text);
				s = add_global(t->text, S_FUNC, func_type(ty_int));
				s->is_extern = 1;
			} else {
				error_at(t, "undeclared identifier '%s'", t->text);
			}
		}
		if (s->kind == S_ENUM) return new_num((unsigned)s->val, ty_int, t);
		if (s->kind == S_TYPEDEF) error_at(t, "unexpected type name '%s'", t->text);
		n = new_node(N_VAR, t);
		n->sym = s;
		n->ty = s->ty;
		return n;
	}
	error_at(t, "expression expected before '%s'", t->kind == T_EOF ? "end of file" : t->text);
	return NULL;
}

static Node *incdec(int kind, Node *l, Tok *t) {
	Node *n = new_node(kind, t);
	Type *lt = l->ty;
	need_lvalue(l);
	if (!is_scalar(lt)) error_at(t, "invalid operand to increment or decrement");
	n->l = l;
	n->ty = lt;
	n->val = lt->kind == TY_PTR ? (unsigned)sizeof_base(lt, t) : 1;
	return n;
}

static Node *postfix(void) {
	Node *n = primary();
	while (1) {
		Tok *t = tok;
		if (consume("[")) {
			Node *idx = expr();
			expect("]");
			n = new_unary(N_DEREF, new_binary(N_ADD, n, idx, t), t);
			continue;
		}
		if (consume("(")) {
			n = funcall(n, t);
			continue;
		}
		if (consume(".")) {
			n = member_ref(n, t);
			continue;
		}
		if (consume("->")) {
			n = new_unary(N_DEREF, n, t);
			n = member_ref(n, t);
			continue;
		}
		if (consume("++")) {
			n = incdec(N_POSTINC, n, t);
			continue;
		}
		if (consume("--")) {
			n = incdec(N_POSTDEC, n, t);
			continue;
		}
		return n;
	}
}

static Type *typename_(void) {
	int sc = 0;
	Type *ty = declspec(&sc);
	if (sc) error_at(tok, "storage class not allowed here");
	return abstract_declarator(ty);
}

static Node *unary(void) {
	Tok *t = tok;
	if (consume("++")) return incdec(N_PREINC, unary(), t);
	if (consume("--")) return incdec(N_PREDEC, unary(), t);
	if (consume("&")) return new_unary(N_ADDR, cast_expr(), t);
	if (consume("*")) return new_unary(N_DEREF, cast_expr(), t);
	if (consume("+")) {
		Node *n = cast_expr();
		if (!is_integer(vtype(n->ty))) error_at(t, "invalid operand to unary '+'");
		return new_cast(n, promote(vtype(n->ty)));
	}
	if (consume("-")) return new_unary(N_NEG, cast_expr(), t);
	if (consume("~")) return new_unary(N_BITNOT, cast_expr(), t);
	if (consume("!")) return new_unary(N_NOT, cast_expr(), t);
	if (equal(tok, "sizeof")) {
		Type *ty;
		tok = tok->next;
		if (equal(tok, "(") && is_typename(tok->next)) {
			tok = tok->next;
			ty = typename_();
			expect(")");
		} else {
			ty = unary()->ty;
		}
		if (ty->kind == TY_FUNC) error_at(t, "sizeof applied to a function");
		if (!ty->complete) error_at(t, "sizeof applied to an incomplete type");
		return new_num((unsigned)ty->size, ty_uint, t);
	}
	return postfix();
}

static Node *cast_expr(void) {
	if (equal(tok, "(") && is_typename(tok->next)) {
		Tok *t = tok;
		Type *ty;
		Node *e, *n;
		tok = tok->next;
		ty = typename_();
		expect(")");
		e = cast_expr();
		if (ty->kind != TY_VOID && !is_scalar(ty)) error_at(t, "cast to a non-scalar type");
		if (ty->kind != TY_VOID && !is_scalar(vtype(e->ty))) error_at(t, "cast of a non-scalar value");
		n = new_node(N_CAST, t);
		n->l = e;
		n->ty = ty;
		return n;
	}
	return unary();
}

static Node *mul(void) {
	Node *n = cast_expr();
	while (1) {
		Tok *t = tok;
		if (consume("*")) n = new_binary(N_MUL, n, cast_expr(), t);
		else if (consume("/")) n = new_binary(N_DIV, n, cast_expr(), t);
		else if (consume("%")) n = new_binary(N_MOD, n, cast_expr(), t);
		else return n;
	}
}

static Node *add(void) {
	Node *n = mul();
	while (1) {
		Tok *t = tok;
		if (consume("+")) n = new_binary(N_ADD, n, mul(), t);
		else if (consume("-")) n = new_binary(N_SUB, n, mul(), t);
		else return n;
	}
}

static Node *shift(void) {
	Node *n = add();
	while (1) {
		Tok *t = tok;
		if (consume("<<")) n = new_binary(N_SHL, n, add(), t);
		else if (consume(">>")) n = new_binary(N_SHR, n, add(), t);
		else return n;
	}
}

static Node *relational(void) {
	Node *n = shift();
	while (1) {
		Tok *t = tok;
		if (consume("<")) n = new_binary(N_LT, n, shift(), t);
		else if (consume("<=")) n = new_binary(N_LE, n, shift(), t);
		else if (consume(">")) n = new_binary(N_GT, n, shift(), t);
		else if (consume(">=")) n = new_binary(N_GE, n, shift(), t);
		else return n;
	}
}

static Node *equality(void) {
	Node *n = relational();
	while (1) {
		Tok *t = tok;
		if (consume("==")) n = new_binary(N_EQ, n, relational(), t);
		else if (consume("!=")) n = new_binary(N_NE, n, relational(), t);
		else return n;
	}
}

static Node *bitand(void) {
	Node *n = equality();
	while (1) {
		Tok *t = tok;
		if (consume("&")) n = new_binary(N_AND, n, equality(), t);
		else return n;
	}
}

static Node *bitxor(void) {
	Node *n = bitand();
	while (1) {
		Tok *t = tok;
		if (consume("^")) n = new_binary(N_XOR, n, bitand(), t);
		else return n;
	}
}

static Node *bitor(void) {
	Node *n = bitxor();
	while (1) {
		Tok *t = tok;
		if (consume("|")) n = new_binary(N_OR, n, bitxor(), t);
		else return n;
	}
}

static Node *logand(void) {
	Node *n = bitor();
	while (1) {
		Tok *t = tok;
		if (consume("&&")) n = new_binary(N_LOGAND, n, bitor(), t);
		else return n;
	}
}

static Node *logor(void) {
	Node *n = logand();
	while (1) {
		Tok *t = tok;
		if (consume("||")) n = new_binary(N_LOGOR, n, logand(), t);
		else return n;
	}
}

static Node *cond_expr(void) {
	Node *c = logor(), *n;
	Tok *t = tok;
	Type *a, *b;
	if (!consume("?")) return c;
	n = new_node(N_COND, t);
	n->c = c;
	n->l = expr();
	expect(":");
	n->r = cond_expr();
	if (!is_scalar(vtype(c->ty))) error_at(t, "condition of '?' is not a scalar");
	a = vtype(n->l->ty);
	b = vtype(n->r->ty);
	if (is_integer(a) && is_integer(b)) {
		n->ty = arith_conv(a, b);
	} else if (a->kind == TY_PTR) {
		n->ty = a;
		if (b->kind == TY_PTR && a->base->kind == TY_VOID) n->ty = b;
	} else if (b->kind == TY_PTR) {
		n->ty = b;
	} else if (a->kind == TY_VOID || b->kind == TY_VOID) {
		n->ty = ty_void;
	} else if ((a->kind == TY_STRUCT || a->kind == TY_UNION) && a == b) {
		n->ty = a;
	} else {
		error_at(t, "incompatible operands of '?:'");
	}
	return n;
}

static Node *opassign(int kind, Node *l, Node *r, Tok *t) {
	Node *n = new_node(N_OPASSIGN, t);
	Type *lt = l->ty;
	need_lvalue(l);
	if ((kind == N_ADD || kind == N_SUB) && lt->kind == TY_PTR) {
		if (!is_integer(vtype(r->ty))) error_at(t, "invalid operands to compound assignment");
		r = scale(r, sizeof_base(lt, t), t);
		n->op = kind;
		n->size = 0;
	} else {
		Node *chk = new_binary(kind, new_num(0, promote(is_integer(lt) ? lt : ty_int), t), r, t);
		if (!is_integer(lt)) error_at(t, "invalid operands to compound assignment");
		n->op = kind;
		n->size = (kind == N_SHL || kind == N_SHR) ? promote(lt)->uns : arith_conv(lt, vtype(r->ty))->uns;
		(void)chk;
	}
	n->l = l;
	n->r = r;
	n->ty = lt;
	return n;
}

static Node *assign(void) {
	Node *n = cond_expr();
	Tok *t = tok;
	if (consume("=")) return new_assign(n, assign(), t);
	if (consume("+=")) return opassign(N_ADD, n, assign(), t);
	if (consume("-=")) return opassign(N_SUB, n, assign(), t);
	if (consume("*=")) return opassign(N_MUL, n, assign(), t);
	if (consume("/=")) return opassign(N_DIV, n, assign(), t);
	if (consume("%=")) return opassign(N_MOD, n, assign(), t);
	if (consume("&=")) return opassign(N_AND, n, assign(), t);
	if (consume("|=")) return opassign(N_OR, n, assign(), t);
	if (consume("^=")) return opassign(N_XOR, n, assign(), t);
	if (consume("<<=")) return opassign(N_SHL, n, assign(), t);
	if (consume(">>=")) return opassign(N_SHR, n, assign(), t);
	return n;
}

static Node *expr(void) {
	Node *n = assign();
	while (1) {
		Tok *t = tok;
		if (consume(",")) n = new_binary(N_COMMA, n, assign(), t);
		else return n;
	}
}

static int eval_const(Node *n, char **label, int must) {
	char *l2 = NULL;
	int a, b;
	switch (n->kind) {
	case N_NUM:
		return (int)n->val;
	case N_CAST:
		a = eval_const(n->l, label, must);
		if (n->ty->kind == TY_CHAR) a = n->ty->uns ? (a & 0xFF) : (int)(signed char)a;
		else if (n->ty->kind == TY_SHORT) a = n->ty->uns ? (a & 0xFFFF) : (int)(short)a;
		return a;
	case N_STR:
		if (!label) break;
		*label = n->label;
		return 0;
	case N_VAR:
		if (!label) break;
		if ((n->sym->kind == S_GLOBAL || n->sym->kind == S_FUNC) && (n->ty->kind == TY_ARRAY || n->ty->kind == TY_FUNC)) {
			*label = n->sym->label;
			return 0;
		}
		break;
	case N_ADDR:
		if (!label) break;
		if (n->l->kind == N_VAR && (n->l->sym->kind == S_GLOBAL || n->l->sym->kind == S_FUNC)) {
			*label = n->l->sym->label;
			return 0;
		}
		if (n->l->kind == N_DEREF) return eval_const(n->l->l, label, must);
		break;
	case N_DEREF:
		if (label && n->ty->kind == TY_ARRAY) return eval_const(n->l, label, must);
		break;
	case N_ADD:
		a = eval_const(n->l, label, must);
		b = eval_const(n->r, label && !*label ? label : NULL, must);
		return a + b;
	case N_SUB:
		a = eval_const(n->l, label, must);
		b = eval_const(n->r, NULL, must);
		return a - b;
	case N_MUL: return eval_const(n->l, NULL, must) * eval_const(n->r, NULL, must);
	case N_DIV:
		b = eval_const(n->r, NULL, must);
		if (!b) error_at(n->tok, "division by zero in constant expression");
		a = eval_const(n->l, NULL, must);
		return n->ty->uns ? (int)((unsigned)a / (unsigned)b) : a / b;
	case N_MOD:
		b = eval_const(n->r, NULL, must);
		if (!b) error_at(n->tok, "division by zero in constant expression");
		a = eval_const(n->l, NULL, must);
		return n->ty->uns ? (int)((unsigned)a % (unsigned)b) : a % b;
	case N_AND: return eval_const(n->l, NULL, must) & eval_const(n->r, NULL, must);
	case N_OR: return eval_const(n->l, NULL, must) | eval_const(n->r, NULL, must);
	case N_XOR: return eval_const(n->l, NULL, must) ^ eval_const(n->r, NULL, must);
	case N_SHL: return eval_const(n->l, NULL, must) << eval_const(n->r, NULL, must);
	case N_SHR:
		a = eval_const(n->l, NULL, must);
		b = eval_const(n->r, NULL, must);
		return n->ty->uns ? (int)((unsigned)a >> b) : a >> b;
	case N_EQ: return eval_const(n->l, NULL, must) == eval_const(n->r, NULL, must);
	case N_NE: return eval_const(n->l, NULL, must) != eval_const(n->r, NULL, must);
	case N_LT: case N_LE: case N_GT: case N_GE:
		a = eval_const(n->l, NULL, must);
		b = eval_const(n->r, NULL, must);
		if (n->op) {
			unsigned ua = (unsigned)a, ub = (unsigned)b;
			if (n->kind == N_LT) return ua < ub;
			if (n->kind == N_LE) return ua <= ub;
			if (n->kind == N_GT) return ua > ub;
			return ua >= ub;
		}
		if (n->kind == N_LT) return a < b;
		if (n->kind == N_LE) return a <= b;
		if (n->kind == N_GT) return a > b;
		return a >= b;
	case N_LOGAND: return eval_const(n->l, NULL, must) && eval_const(n->r, NULL, must);
	case N_LOGOR: return eval_const(n->l, NULL, must) || eval_const(n->r, NULL, must);
	case N_NOT: return !eval_const(n->l, NULL, must);
	case N_BITNOT: return ~eval_const(n->l, NULL, must);
	case N_NEG: return -eval_const(n->l, NULL, must);
	case N_COND:
		return eval_const(n->c, NULL, must) ? eval_const(n->l, label, must) : eval_const(n->r, label, must);
	case N_COMMA:
		return eval_const(n->r, label, must);
	}
	(void)l2;
	if (must) error_at(n->tok, "constant expression required");
	return 0;
}

static unsigned const_expr(void) {
	Node *n = cond_expr();
	return (unsigned)eval_const(n, NULL, 1);
}

static int is_typename(Tok *t) {
	static const char *kw[] = {"void", "char", "short", "int", "long", "signed", "unsigned", "struct",
		"union", "enum", "const", "volatile", "typedef", "extern", "static", "auto", "register", "inline", NULL};
	int i;
	Sym *s;
	if (t->kind != T_IDENT) return 0;
	for (i = 0; kw[i]; i++)
		if (strcmp(t->text, kw[i]) == 0) return 1;
	s = find_sym(t->text);
	return s && s->kind == S_TYPEDEF;
}

static void add_tag(char *name, Type *ty) {
	Tag *g = xmalloc(sizeof(Tag));
	g->name = name;
	g->ty = ty;
	g->depth = depth;
	g->next = tags;
	tags = g;
}

static Type *struct_decl(int is_union) {
	Tok *t = tok;
	char *name = NULL;
	Type *ty = NULL;
	Member head, *cur = &head;
	int off = 0, align = 1, size = 0;
	if (tok->kind == T_IDENT) {
		name = tok->text;
		tok = tok->next;
	}
	if (name && !equal(tok, "{")) {
		Tag *g = find_tag(name);
		if (g) {
			if (g->ty->kind != (is_union ? TY_UNION : TY_STRUCT)) error_at(t, "'%s' defined as a different kind of tag", name);
			return g->ty;
		}
		ty = new_type(is_union ? TY_UNION : TY_STRUCT, 0, 1);
		ty->complete = 0;
		ty->tag = name;
		add_tag(name, ty);
		return ty;
	}
	expect("{");
	if (name) {
		Tag *g = find_tag(name);
		if (g && g->depth == depth) {
			if (g->ty->complete) error_at(t, "redefinition of '%s'", name);
			ty = g->ty;
		}
	}
	if (!ty) {
		ty = new_type(is_union ? TY_UNION : TY_STRUCT, 0, 1);
		ty->tag = name;
		ty->complete = 0;
		if (name) add_tag(name, ty);
	}
	head.next = NULL;
	while (!consume("}")) {
		int sc = 0;
		Type *base = declspec(&sc);
		if (sc) error_at(tok, "storage class in struct member");
		if (consume(";")) {
			if ((base->kind == TY_STRUCT || base->kind == TY_UNION) && base->complete) {
				Member *m;
				int al = base->align;
				int moff;
				if (!is_union) off = (off + al - 1) & ~(al - 1);
				moff = is_union ? 0 : off;
				for (m = base->members; m; m = m->next) {
					Member *c = xmalloc(sizeof(Member));
					*c = *m;
					c->off = moff + m->off;
					c->next = NULL;
					cur->next = c;
					cur = c;
				}
				if (al > align) align = al;
				if (is_union) {
					if (base->size > size) size = base->size;
				} else {
					off += base->size;
				}
				continue;
			}
			error_at(tok, "declaration does not declare anything");
		}
		do {
			char *mname = NULL;
			Tok *mt = tok;
			Type *mty = declarator(base, &mname, &mt);
			Member *m;
			if (equal(tok, ":")) error_at(tok, "bitfields are not supported");
			if (!mty->complete) error_at(mt, "member '%s' has incomplete type", mname ? mname : "");
			if (mty->kind == TY_FUNC) error_at(mt, "member '%s' declared as a function", mname ? mname : "");
			m = xmalloc(sizeof(Member));
			m->name = mname;
			m->ty = mty;
			if (is_union) {
				m->off = 0;
				if (mty->size > size) size = mty->size;
			} else {
				off = (off + mty->align - 1) & ~(mty->align - 1);
				m->off = off;
				off += mty->size;
			}
			if (mty->align > align) align = mty->align;
			cur->next = m;
			cur = m;
		} while (consume(","));
		expect(";");
	}
	if (!is_union) size = off;
	ty->members = head.next;
	ty->align = align;
	ty->size = (size + align - 1) & ~(align - 1);
	ty->complete = 1;
	return ty;
}

static Type *enum_decl(void) {
	Tok *t = tok;
	char *name = NULL;
	int v = 0;
	Type *ty;
	if (tok->kind == T_IDENT) {
		name = tok->text;
		tok = tok->next;
	}
	if (name && !equal(tok, "{")) {
		Tag *g = find_tag(name);
		if (g) return g->ty;
		ty = new_type(TY_ENUM, 4, 4);
		add_tag(name, ty);
		return ty;
	}
	(void)t;
	expect("{");
	ty = new_type(TY_ENUM, 4, 4);
	if (name) add_tag(name, ty);
	while (!consume("}")) {
		Tok *et = tok;
		char *en = expect_ident();
		Sym *s;
		if (consume("=")) v = (int)const_expr();
		if (depth == 0) {
			s = find_global(en);
			if (s) error_at(et, "redeclaration of '%s'", en);
			s = add_global(en, S_ENUM, ty_int);
		} else {
			check_redecl_local(en, et);
			s = add_local(en, S_ENUM, ty_int);
		}
		s->val = v++;
		if (!consume(",")) {
			expect("}");
			break;
		}
	}
	return ty;
}

static Type *declspec(int *sclass) {
	int nchar = 0, nshort = 0, nint = 0, nlong = 0, nsigned = 0, nunsigned = 0, nvoid = 0;
	Type *ty = NULL;
	Tok *start = tok;
	while (tok->kind == T_IDENT) {
		Tok *t = tok;
		if (equal(t, "typedef") || equal(t, "extern") || equal(t, "static") || equal(t, "auto") || equal(t, "register")) {
			int c = equal(t, "typedef") ? 1 : equal(t, "extern") ? 2 : equal(t, "static") ? 3 : 4;
			if (*sclass && *sclass != c) error_at(t, "multiple storage classes");
			*sclass = c;
			tok = t->next;
			continue;
		}
		if (equal(t, "const") || equal(t, "volatile") || equal(t, "inline")) {
			tok = t->next;
			continue;
		}
		if (equal(t, "void")) nvoid++;
		else if (equal(t, "char")) nchar++;
		else if (equal(t, "short")) nshort++;
		else if (equal(t, "int")) nint++;
		else if (equal(t, "long")) nlong++;
		else if (equal(t, "signed")) nsigned++;
		else if (equal(t, "unsigned")) nunsigned++;
		else if (equal(t, "struct") || equal(t, "union")) {
			if (ty) error_at(t, "two or more data types in declaration");
			tok = t->next;
			ty = struct_decl(equal(t, "union"));
			continue;
		} else if (equal(t, "enum")) {
			if (ty) error_at(t, "two or more data types in declaration");
			tok = t->next;
			ty = enum_decl();
			continue;
		} else {
			Sym *s = find_sym(t->text);
			if (s && s->kind == S_TYPEDEF && !ty && !nvoid && !nchar && !nshort && !nint && !nlong && !nsigned && !nunsigned) {
				ty = s->ty;
				tok = t->next;
				continue;
			}
			break;
		}
		if (nlong > 1) error_at(t, "long long is not supported");
		if (equal(t, "float") || equal(t, "double")) error_at(t, "floating point is not supported");
		tok = t->next;
	}
	if (tok->kind == T_IDENT && (equal(tok, "float") || equal(tok, "double"))) error_at(tok, "floating point is not supported");
	if (ty) {
		if (nvoid || nchar || nshort || nint || nlong || nsigned || nunsigned) error_at(start, "two or more data types in declaration");
		return ty;
	}
	if (nsigned && nunsigned) error_at(start, "both signed and unsigned");
	if (nvoid) {
		if (nchar || nshort || nint || nlong || nsigned || nunsigned) error_at(start, "invalid void declaration");
		return ty_void;
	}
	if (nchar) {
		if (nshort || nint || nlong) error_at(start, "invalid char declaration");
		return nunsigned ? ty_uchar : ty_char;
	}
	if (nshort) {
		if (nlong) error_at(start, "both short and long");
		return nunsigned ? ty_ushort : ty_short;
	}
	if (nint || nlong || nsigned || nunsigned) return nunsigned ? ty_uint : ty_int;
	if (*sclass) return ty_int;
	error_at(start, "type name expected before '%s'", start->text);
	return ty_int;
}

static Type *params_(Type *ret) {
	Type *ft = func_type(ret);
	Param head, *cur = &head;
	head.next = NULL;
	ft->proto = 1;
	if (equal(tok, "void") && equal(tok->next, ")")) {
		tok = tok->next->next;
		return ft;
	}
	if (consume(")")) {
		ft->proto = 0;
		return ft;
	}
	while (1) {
		int sc = 0;
		char *name = NULL;
		Tok *pt = tok;
		Type *ty;
		Param *p;
		if (consume("...")) {
			ft->variadic = 1;
			expect(")");
			break;
		}
		if (!is_typename(tok)) error_at(tok, "parameter type expected (K&R definitions are not supported)");
		ty = declspec(&sc);
		ty = declarator(ty, &name, &pt);
		if (ty->kind == TY_ARRAY) ty = ptr_to(ty->base);
		if (ty->kind == TY_FUNC) ty = ptr_to(ty);
		if (ty->kind == TY_VOID) error_at(pt, "parameter has void type");
		p = xmalloc(sizeof(Param));
		p->name = name;
		p->ty = ty;
		cur->next = p;
		cur = p;
		if (consume(")")) break;
		expect(",");
	}
	ft->params = head.next;
	return ft;
}

static Type *type_suffix(Type *ty) {
	if (consume("(")) {
		if (ty->kind == TY_FUNC) error_at(tok, "function returning a function");
		if (ty->kind == TY_ARRAY) error_at(tok, "function returning an array");
		return params_(ty);
	}
	if (consume("[")) {
		int len = -1;
		Tok *t = tok;
		if (!equal(tok, "]")) {
			len = (int)const_expr();
			if (len < 0) error_at(t, "negative array size");
		}
		expect("]");
		ty = type_suffix(ty);
		if (ty->kind == TY_FUNC) error_at(t, "array of functions");
		if (!ty->complete) error_at(t, "array of incomplete type");
		return array_of(ty, len);
	}
	return ty;
}

static Type *declarator(Type *ty, char **name, Tok **ntok) {
	while (consume("*")) {
		ty = ptr_to(ty);
		while (consume("const") || consume("volatile")) ;
	}
	if (equal(tok, "(") && (equal(tok->next, "*") || equal(tok->next, "(") || (tok->next->kind == T_IDENT && !is_typename(tok->next)))) {
		Tok *start, *end;
		Type dummy;
		char *dn = NULL;
		Tok *dt = NULL;
		tok = tok->next;
		start = tok;
		memset(&dummy, 0, sizeof(dummy));
		dummy.complete = 1;
		dummy.size = 1;
		dummy.align = 1;
		declarator(&dummy, &dn, &dt);
		expect(")");
		ty = type_suffix(ty);
		end = tok;
		tok = start;
		ty = declarator(ty, name, ntok);
		expect(")");
		tok = end;
		return ty;
	}
	if (tok->kind == T_IDENT && !is_typename(tok)) {
		*name = tok->text;
		*ntok = tok;
		tok = tok->next;
	}
	return type_suffix(ty);
}

static Type *abstract_declarator(Type *ty) {
	char *name = NULL;
	Tok *t = tok;
	ty = declarator(ty, &name, &t);
	if (name) error_at(t, "unexpected name '%s' in type name", name);
	return ty;
}


static void add_linit(LInit **tail, int off, Type *ty, Node *e) {
	LInit *li = xmalloc(sizeof(LInit));
	li->off = off;
	li->ty = ty;
	li->expr = e;
	(*tail)->next = li;
	*tail = li;
}

static void initializer(Type *ty, int off, LInit **tail, int top);

static int str_init(Type *ty, int off, LInit **tail) {
	Tok *t = tok;
	Node *s;
	StrLit *sl;
	int i, n;
	if (!(ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR && tok->kind == T_STR)) return 0;
	s = string_lit();
	sl = strlits;
	strlits = sl->next;
	n = (int)s->val + 1;
	if (ty->len < 0) {
		ty->len = n;
		ty->size = n;
		ty->complete = 1;
	}
	if (n - 1 > ty->len) error_at(t, "initializer string too long");
	for (i = 0; i < ty->len && i < n; i++)
		add_linit(tail, off + i, ty->base, new_num((unsigned)(int)(signed char)(i < n - 1 ? sl->data[i] : 0), ty_int, t));
	return 1;
}

static void aggr_elems(Type *ty, int off, LInit **tail, int braced) {
	if (ty->kind == TY_ARRAY) {
		int i = 0;
		while (!equal(tok, "}") && tok->kind != T_EOF) {
			if (ty->len >= 0 && i >= ty->len) {
				if (braced) error_at(tok, "too many elements in array initializer");
				return;
			}
			initializer(ty->base, off + i * ty->base->size, tail, 0);
			i++;
			if (equal(tok, "}")) break;
			if (!braced && ty->len >= 0 && i >= ty->len) break;
			expect(",");
			if (!braced && equal(tok, "}")) break;
		}
		if (ty->len < 0) {
			ty->len = i;
			ty->size = i * ty->base->size;
			ty->complete = 1;
		}
		return;
	}
	if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
		Member *m = ty->members;
		while (!equal(tok, "}") && tok->kind != T_EOF) {
			if (!m) {
				if (braced) error_at(tok, "too many elements in struct initializer");
				return;
			}
			initializer(m->ty, off + m->off, tail, 0);
			m = ty->kind == TY_UNION ? NULL : m->next;
			if (equal(tok, "}")) break;
			if (!braced && !m) break;
			expect(",");
			if (!braced && equal(tok, "}")) break;
		}
	}
}

static void initializer(Type *ty, int off, LInit **tail, int top) {
	if (str_init(ty, off, tail)) return;
	if (is_aggr(ty)) {
		if (consume("{")) {
			if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR && tok->kind == T_STR) {
				str_init(ty, off, tail);
				consume(",");
				expect("}");
				return;
			}
			aggr_elems(ty, off, tail, 1);
			consume(",");
			expect("}");
			return;
		}
		if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
			Tok *save = tok;
			Node *e = assign();
			if (e->ty->kind == ty->kind && e->ty->members == ty->members) {
				add_linit(tail, off, ty, e);
				return;
			}
			tok = save;
		}
		if (top) error_at(tok, "invalid initializer");
		aggr_elems(ty, off, tail, 0);
		return;
	}
	if (consume("{")) {
		initializer(ty, off, tail, 0);
		consume(",");
		expect("}");
		return;
	}
	{
		Node *e = assign();
		add_linit(tail, off, ty, conv_assign(e, ty, e->tok));
	}
}

static Node *var_node(Sym *s, Tok *t) {
	Node *n = new_node(N_VAR, t);
	n->sym = s;
	n->ty = s->ty;
	return n;
}

static Node *at_offset(Sym *s, int off, Type *ty, Tok *t) {
	Node *a, *n;
	if (off == 0 && ty == s->ty) return var_node(s, t);
	a = new_node(N_ADD, t);
	a->l = new_unary(N_ADDR, var_node(s, t), t);
	a->r = new_num((unsigned)off, ty_int, t);
	a->ty = ptr_to(ty);
	n = new_node(N_DEREF, t);
	n->l = a;
	n->ty = ty;
	return n;
}

static GVar *new_gvar(Sym *s) {
	GVar *g = xmalloc(sizeof(GVar));
	g->sym = s;
	if (gvar_tail) gvar_tail->next = g;
	else gvars = g;
	gvar_tail = g;
	return g;
}

static GVar *find_gvar(Sym *s) {
	GVar *g;
	for (g = gvars; g; g = g->next)
		if (g->sym == s) return g;
	return new_gvar(s);
}

static void global_init(GVar *g, Type *ty, Tok *t) {
	LInit head, *tail = &head, *li;
	Init ihead, *it = &ihead;
	head.next = NULL;
	ihead.next = NULL;
	initializer(ty, 0, &tail, 1);
	for (li = head.next; li; li = li->next) {
		Init *in = xmalloc(sizeof(Init));
		char *label = NULL;
		if (!is_scalar(li->ty)) error_at(li->expr->tok, "initializer element is not constant");
		in->off = li->off;
		in->size = li->ty->size;
		in->val = (unsigned)eval_const(li->expr, &label, 1);
		in->label = label;
		if (label && in->size != 4) error_at(li->expr->tok, "address does not fit in initializer");
		it->next = in;
		it = in;
	}
	g->inits = ihead.next;
	g->has_init = 1;
	(void)t;
}

static Node *local_decl(Type *base, int sclass) {
	Node head, *cur = &head;
	head.next = NULL;
	do {
		char *name = NULL;
		Tok *nt = tok;
		Type *ty = declarator(base, &name, &nt);
		Sym *s;
		if (!name) error_at(nt, "variable name expected");
		if (sclass == 1) {
			check_redecl_local(name, nt);
			add_local(name, S_TYPEDEF, ty);
			continue;
		}
		if (ty->kind == TY_FUNC) {
			Sym *g = find_global(name);
			if (!g) {
				g = add_global(name, S_FUNC, ty);
				g->is_extern = 1;
			}
			s = add_local(name, S_FUNC, g->ty);
			s->label = g->label;
			continue;
		}
		if (ty->kind == TY_VOID) error_at(nt, "variable '%s' declared void", name);
		check_redecl_local(name, nt);
		if (sclass == 2) {
			Sym *g = find_global(name);
			if (!g) {
				g = add_global(name, S_GLOBAL, ty);
				g->is_extern = 1;
				find_gvar(g);
			}
			s = add_local(name, S_GLOBAL, g->ty);
			s->label = g->label;
			continue;
		}
		if (sclass == 3) {
			GVar *g;
			s = add_local(name, S_GLOBAL, ty);
			s->label = fmt("_S%d_%s", nstatic++, name);
			s->is_static = 1;
			s->defined = 1;
			g = new_gvar(s);
			if (consume("=")) global_init(g, ty, nt);
			if (!ty->complete) error_at(nt, "storage size of '%s' is unknown", name);
			continue;
		}
		s = add_local(name, S_LOCAL, ty);
		if (consume("=")) {
			LInit lh, *tail = &lh, *li;
			Node *blk = new_node(N_BLOCK, nt);
			Node bh, *bc = &bh;
			lh.next = NULL;
			bh.next = NULL;
			initializer(ty, 0, &tail, 1);
			if (!ty->complete) error_at(nt, "storage size of '%s' is unknown", name);
			s->off = alloc_local(ty);
			if (is_aggr(ty) && !(lh.next && lh.next->ty == ty)) {
				Node *z = new_node(N_MEMZERO, nt);
				z->sym = s;
				z->size = ty->size;
				bc->next = z;
				bc = z;
			}
			for (li = lh.next; li; li = li->next) {
				Node *e = new_node(N_EXPR, nt);
				Node *a = new_node(N_ASSIGN, nt);
				a->l = at_offset(s, li->off, li->ty, nt);
				a->r = li->expr;
				a->ty = li->ty;
				e->l = a;
				bc->next = e;
				bc = e;
			}
			blk->body = bh.next;
			cur->next = blk;
			cur = blk;
		} else {
			if (!ty->complete) error_at(nt, "storage size of '%s' is unknown", name);
			s->off = alloc_local(ty);
		}
	} while (consume(","));
	expect(";");
	{
		Node *b = new_node(N_BLOCK, tok);
		b->body = head.next;
		return b;
	}
}

static LabelRef *get_label(char *name, Tok *t) {
	LabelRef *l;
	for (l = labels; l; l = l->next)
		if (strcmp(l->name, name) == 0) return l;
	l = xmalloc(sizeof(LabelRef));
	l->name = name;
	l->lab = new_label();
	l->tok = t;
	l->next = labels;
	labels = l;
	return l;
}

static Node *cond_of(void) {
	Node *c;
	expect("(");
	c = expr();
	expect(")");
	if (!is_scalar(vtype(c->ty))) error_at(c->tok, "scalar value required in condition");
	return c;
}

static Node *stmt(void) {
	Tok *t = tok;
	Node *n;
	if (consume("{")) return compound();
	if (consume(";")) return new_node(N_NULL, t);
	if (equal(t, "if")) {
		tok = t->next;
		n = new_node(N_IF, t);
		n->c = cond_of();
		n->l = stmt();
		if (consume("else")) n->r = stmt();
		return n;
	}
	if (equal(t, "while")) {
		int sb = brk_lab, sc = cont_lab;
		tok = t->next;
		n = new_node(N_WHILE, t);
		n->c = cond_of();
		n->brk = brk_lab = new_label();
		n->cont = cont_lab = new_label();
		n->body = stmt();
		brk_lab = sb;
		cont_lab = sc;
		return n;
	}
	if (equal(t, "do")) {
		int sb = brk_lab, sc = cont_lab;
		tok = t->next;
		n = new_node(N_DO, t);
		n->brk = brk_lab = new_label();
		n->cont = cont_lab = new_label();
		n->body = stmt();
		brk_lab = sb;
		cont_lab = sc;
		if (!consume("while")) error_at(tok, "expected 'while' after do body");
		n->c = cond_of();
		expect(";");
		return n;
	}
	if (equal(t, "for")) {
		int sb = brk_lab, sc = cont_lab, save_off = cur_off;
		tok = t->next;
		n = new_node(N_FOR, t);
		expect("(");
		enter_scope();
		if (!consume(";")) {
			if (is_typename(tok)) {
				int scl = 0;
				Type *b = declspec(&scl);
				n->init = local_decl(b, scl);
			} else {
				n->init = new_node(N_EXPR, tok);
				n->init->l = expr();
				expect(";");
			}
		}
		if (!equal(tok, ";")) {
			n->c = expr();
			if (!is_scalar(vtype(n->c->ty))) error_at(n->c->tok, "scalar value required in condition");
		}
		expect(";");
		if (!equal(tok, ")")) n->inc = expr();
		expect(")");
		n->brk = brk_lab = new_label();
		n->cont = cont_lab = new_label();
		n->body = stmt();
		brk_lab = sb;
		cont_lab = sc;
		leave_scope(save_off);
		return n;
	}
	if (equal(t, "switch")) {
		Node *ssw = cur_switch;
		int sb = brk_lab;
		tok = t->next;
		n = new_node(N_SWITCH, t);
		n->c = cond_of();
		if (!is_integer(vtype(n->c->ty))) error_at(t, "switch quantity is not an integer");
		n->brk = brk_lab = new_label();
		cur_switch = n;
		n->body = stmt();
		cur_switch = ssw;
		brk_lab = sb;
		return n;
	}
	if (equal(t, "case")) {
		Node *c;
		tok = t->next;
		if (!cur_switch) error_at(t, "case label not within a switch statement");
		n = new_node(N_CASE, t);
		n->val = const_expr();
		expect(":");
		for (c = cur_switch->cases; c; c = c->cnext)
			if (c->val == n->val) error_at(t, "duplicate case value");
		n->lab = new_label();
		n->cnext = cur_switch->cases;
		cur_switch->cases = n;
		n->body = stmt();
		return n;
	}
	if (equal(t, "default")) {
		tok = t->next;
		if (!cur_switch) error_at(t, "default label not within a switch statement");
		if (cur_switch->deflt) error_at(t, "multiple default labels in one switch");
		expect(":");
		n = new_node(N_DEFAULT, t);
		n->lab = new_label();
		cur_switch->deflt = n;
		n->body = stmt();
		return n;
	}
	if (equal(t, "break")) {
		tok = t->next;
		if (!brk_lab) error_at(t, "break statement not within loop or switch");
		n = new_node(N_BREAK, t);
		n->lab = brk_lab;
		expect(";");
		return n;
	}
	if (equal(t, "continue")) {
		tok = t->next;
		if (!cont_lab) error_at(t, "continue statement not within a loop");
		n = new_node(N_CONTINUE, t);
		n->lab = cont_lab;
		expect(";");
		return n;
	}
	if (equal(t, "return")) {
		Type *rt = cur_func->sym->ty->base;
		tok = t->next;
		n = new_node(N_RETURN, t);
		if (!consume(";")) {
			Node *e = expr();
			expect(";");
			if (rt->kind == TY_VOID) {
				if (vtype(e->ty)->kind != TY_VOID) error_at(t, "return with a value in a function returning void");
				n->l = e;
				n->op = 1;
			} else {
				n->l = conv_assign(e, rt, t);
			}
		} else if (rt->kind != TY_VOID) {
			error_at(t, "return with no value in a function returning non-void");
		}
		return n;
	}
	if (equal(t, "goto")) {
		LabelRef *l;
		tok = t->next;
		n = new_node(N_GOTO, t);
		l = get_label(expect_ident(), t);
		n->lab = l->lab;
		expect(";");
		return n;
	}
	if (t->kind == T_IDENT && equal(t->next, ":") && !is_typename(t)) {
		LabelRef *l = get_label(t->text, t);
		if (l->defined) error_at(t, "duplicate label '%s'", t->text);
		l->defined = 1;
		tok = t->next->next;
		n = new_node(N_LABEL, t);
		n->lab = l->lab;
		n->body = stmt();
		return n;
	}
	n = new_node(N_EXPR, t);
	n->l = expr();
	expect(";");
	return n;
}

static Node *compound(void) {
	Node *blk = new_node(N_BLOCK, tok);
	Node head, *cur = &head;
	int save_off = cur_off;
	head.next = NULL;
	enter_scope();
	while (!consume("}")) {
		Node *s;
		if (tok->kind == T_EOF) error_at(tok, "unexpected end of file in block");
		if (is_typename(tok) && !equal(tok->next, ":")) {
			int sc = 0;
			Type *b = declspec(&sc);
			if (consume(";")) continue;
			s = local_decl(b, sc);
		} else {
			s = stmt();
		}
		cur->next = s;
		cur = s;
	}
	leave_scope(save_off);
	blk->body = head.next;
	return blk;
}

static int same_type(Type *a, Type *b) {
	Param *p, *q;
	if (a == b) return 1;
	if (a->kind != b->kind) return 0;
	switch (a->kind) {
	case TY_CHAR: case TY_SHORT: case TY_INT: return a->uns == b->uns;
	case TY_ENUM: return 1;
	case TY_PTR: return same_type(a->base, b->base) || a->base->kind == TY_VOID || b->base->kind == TY_VOID;
	case TY_ARRAY: return same_type(a->base, b->base) && (a->len < 0 || b->len < 0 || a->len == b->len);
	case TY_FUNC:
		if (!same_type(a->base, b->base)) return 0;
		if (!a->proto || !b->proto) return 1;
		if (a->variadic != b->variadic) return 0;
		for (p = a->params, q = b->params; p && q; p = p->next, q = q->next)
			if (!same_type(p->ty, q->ty)) return 0;
		return !p && !q;
	}
	return 0;
}

static void function(Type *ty, char *name, int sclass, Tok *nt) {
	Sym *s = find_global(name);
	Func *f;
	Param *p;
	int off;
	LabelRef *l;
	if (s) {
		if (s->kind != S_FUNC) error_at(nt, "'%s' redeclared as a different kind of symbol", name);
		if (s->defined) error_at(nt, "redefinition of '%s'", name);
		if (!same_type(s->ty, ty)) error_at(nt, "conflicting types for '%s'", name);
		s->ty = ty;
	} else {
		s = add_global(name, S_FUNC, ty);
	}
	s->defined = 1;
	s->is_extern = 0;
	if (sclass == 3) s->is_static = 1;
	f = xmalloc(sizeof(Func));
	f->sym = s;
	cur_func = f;
	locals = NULL;
	depth = 1;
	cur_off = max_off = 0;
	labels = NULL;
	brk_lab = cont_lab = 0;
	off = 8;
	if (ty->base->kind == TY_STRUCT || ty->base->kind == TY_UNION) {
		f->sret = 1;
		off = 12;
	}
	for (p = ty->params; p; p = p->next) {
		Sym *ps;
		if (!p->name) error_at(nt, "parameter name omitted");
		check_redecl_local(p->name, nt);
		ps = add_local(p->name, S_LOCAL, p->ty);
		ps->off = off;
		off += (p->ty->size + 3) & ~3;
	}
	f->params = ty->params;
	f->pvars = locals;
	expect("{");
	f->body = compound();
	f->frame = (max_off + 3) & ~3;
	for (l = labels; l; l = l->next)
		if (!l->defined) error_at(l->tok, "label '%s' used but not defined", l->name);
	locals = NULL;
	depth = 0;
	cur_func = NULL;
	if (func_tail) func_tail->next = f;
	else funcs = f;
	func_tail = f;
}

static void global_decl(Type *base, int sclass) {
	do {
		char *name = NULL;
		Tok *nt = tok;
		Type *ty = declarator(base, &name, &nt);
		Sym *s;
		GVar *g;
		if (!name) error_at(nt, "identifier expected in declaration");
		if (sclass == 1) {
			s = find_global(name);
			if (s && (s->kind != S_TYPEDEF || !same_type(s->ty, ty))) error_at(nt, "redefinition of '%s'", name);
			if (!s) add_global(name, S_TYPEDEF, ty);
			continue;
		}
		if (ty->kind == TY_FUNC) {
			if (equal(tok, "{")) {
				function(ty, name, sclass, nt);
				return;
			}
			s = find_global(name);
			if (s) {
				if (s->kind != S_FUNC) error_at(nt, "'%s' redeclared as a different kind of symbol", name);
				if (!same_type(s->ty, ty)) error_at(nt, "conflicting types for '%s'", name);
				if (ty->proto && !s->defined) s->ty = ty;
			} else {
				s = add_global(name, S_FUNC, ty);
				s->is_extern = 1;
			}
			if (sclass == 3) s->is_static = 1;
			continue;
		}
		if (ty->kind == TY_VOID) error_at(nt, "variable '%s' declared void", name);
		s = find_global(name);
		if (s) {
			if (s->kind != S_GLOBAL) error_at(nt, "'%s' redeclared as a different kind of symbol", name);
			if (!same_type(s->ty, ty)) error_at(nt, "conflicting types for '%s'", name);
			if (!s->ty->complete && ty->complete) s->ty = ty;
		} else {
			s = add_global(name, S_GLOBAL, ty);
		}
		g = find_gvar(s);
		if (sclass == 3) s->is_static = 1;
		if (consume("=")) {
			if (g->has_init) error_at(nt, "redefinition of '%s'", name);
			global_init(g, s->ty, nt);
			s->defined = 1;
		} else if (sclass != 2) {
			s->tentative = 1;
		}
		if ((s->defined || s->tentative) && !s->ty->complete && sclass != 2 && !equal(tok, ",") && !equal(tok, ";")) error_at(nt, "storage size of '%s' is unknown", name);
	} while (consume(","));
	expect(";");
}

void parse_program(Tok *toks) {
	GVar *g;
	tok = toks;
	while (tok->kind != T_EOF) {
		int sc = 0;
		Type *base;
		if (consume(";")) continue;
		base = declspec(&sc);
		if (sc == 4) error_at(tok, "'auto' or 'register' at file scope");
		if (consume(";")) continue;
		global_decl(base, sc);
	}
	for (g = gvars; g; g = g->next) {
		Sym *s = g->sym;
		if ((s->defined || s->tentative) && !s->ty->complete) {
			if (s->ty->kind == TY_ARRAY && s->ty->len < 0) {
				s->ty->len = 1;
				s->ty->size = s->ty->base->size;
				s->ty->complete = 1;
			} else {
				fatal("storage size of '%s' is unknown", s->name);
			}
		}
	}
}
