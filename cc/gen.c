/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "cc.h"

FILE *outf;
static int nlabel = 1;
static Func *gfunc;
static int ret_lab;

int new_label(void) {
	return nlabel++;
}

static void emit(const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	vfprintf(outf, f, ap);
	va_end(ap);
	fputc('\n', outf);
}

static char *sname(char *s) {
	int n = (int)strlen(s);
	if ((n == 2 && (s[0] == 'r' || s[0] == 'R') && s[1] >= '0' && s[1] <= '7') ||
	    (n == 2 && (s[0] == 's' || s[0] == 'S') && (s[1] == 'p' || s[1] == 'P')) ||
	    ((n == 3 || n == 4) && (s[0] == 'c' || s[0] == 'C') && (s[1] == 'r' || s[1] == 'R') && isdigit((unsigned char)s[2])))
		return fmt("(%s)", s);
	return s;
}

static void gen_expr(Node *n);
static void gen_addr(Node *n);
static void gen_stmt(Node *n);

static int is_struct(Type *t) {
	return t->kind == TY_STRUCT || t->kind == TY_UNION;
}

static int noload(Type *t) {
	return t->kind == TY_ARRAY || t->kind == TY_FUNC || is_struct(t);
}

static void load(Type *t) {
	if (noload(t)) return;
	if (t->kind == TY_CHAR) emit(t->uns ? "LDB R0, [R0]" : "LDBS R0, [R0]");
	else if (t->kind == TY_SHORT) emit(t->uns ? "LDH R0, [R0]" : "LDHS R0, [R0]");
	else emit("LDW R0, [R0]");
}

static void store(Type *t) {
	if (t->kind == TY_CHAR) emit("STB [R1], R0");
	else if (t->kind == TY_SHORT) emit("STH [R1], R0");
	else emit("STW [R1], R0");
}

static void copy_mem(int size) {
	int i = 0;
	if (size > 64) {
		int l = new_label();
		emit("LOAD32 R2, %d", size / 4);
		emit(".L%d:", l);
		emit("LDW R3, [R0]");
		emit("STW [R1], R3");
		emit("ADD32 R0, 4");
		emit("ADD32 R1, 4");
		emit("SUB32 R2, 1");
		emit("BNE .L%d", l);
		for (i = 0; i < size % 4; i++) {
			emit("LDB R3, [R0 + %d]", i);
			emit("STB [R1 + %d], R3", i);
		}
		return;
	}
	for (; i + 4 <= size; i += 4) {
		emit("LDW R3, [R0 + %d]", i);
		emit("STW [R1 + %d], R3", i);
	}
	for (; i < size; i++) {
		emit("LDB R3, [R0 + %d]", i);
		emit("STB [R1 + %d], R3", i);
	}
}

static void convert(Type *from, Type *to) {
	if (to->kind == TY_VOID) return;
	if (to->kind == TY_CHAR) {
		if (from->kind == TY_CHAR && from->uns == to->uns) return;
		if (to->uns) emit("AND32 R0, 0xFF");
		else {
			emit("SHL32 R0, 24");
			emit("SAR R0, 24");
		}
	} else if (to->kind == TY_SHORT) {
		if ((from->kind == TY_SHORT && from->uns == to->uns) || (from->kind == TY_CHAR && (from->uns || !to->uns))) return;
		if (to->uns) emit("AND32 R0, 0xFFFF");
		else {
			emit("SHL32 R0, 16");
			emit("SAR R0, 16");
		}
	}
}

static int simple_rhs(Node *n) {
	if (n->kind == N_NUM) return 1;
	if (n->kind == N_VAR && n->sym->kind == S_LOCAL && n->ty->kind == TY_INT) return 1;
	if (n->kind == N_CAST && n->l->kind == N_NUM) return 1;
	return 0;
}

static void gen_r1(Node *n) {
	if (n->kind == N_CAST) n = n->l;
	if (n->kind == N_NUM) emit("LOAD32 R1, %u", n->val);
	else emit("LDW R1, [R7 + %d]", n->sym->off);
}

static void binop(int kind, int uns) {
	switch (kind) {
	case N_ADD: emit("ADD32 R0, R1"); break;
	case N_SUB: emit("SUB32 R0, R1"); break;
	case N_MUL: emit("IMUL R0, R1"); break;
	case N_DIV: emit(uns ? "DIV32 R0, R1" : "IDIV R0, R1"); break;
	case N_MOD: emit(uns ? "UMOD R0, R1" : "IMOD R0, R1"); break;
	case N_AND: emit("AND32 R0, R1"); break;
	case N_OR: emit("OR32 R0, R1"); break;
	case N_XOR: emit("XOR32 R0, R1"); break;
	case N_SHL: emit("SHLV R0, R1"); break;
	case N_SHR: emit(uns ? "SHRV R0, R1" : "SARV R0, R1"); break;
	}
}

static const char *cmp_branch(int kind, int uns, int neg) {
	if (neg) {
		switch (kind) {
		case N_EQ: kind = N_NE; break;
		case N_NE: kind = N_EQ; break;
		case N_LT: kind = N_GE; break;
		case N_GE: kind = N_LT; break;
		case N_LE: kind = N_GT; break;
		case N_GT: kind = N_LE; break;
		}
	}
	switch (kind) {
	case N_EQ: return "BEQ";
	case N_NE: return "BNE";
	case N_LT: return uns ? "BLO" : "BLT";
	case N_GE: return uns ? "BHS" : "BGE";
	case N_LE: return uns ? "BLS" : "BLE";
	default: return uns ? "BHI" : "BGT";
	}
}

static void gen_cmp_operands(Node *n) {
	gen_expr(n->l);
	if (simple_rhs(n->r)) {
		gen_r1(n->r);
	} else {
		emit("PUSH32 R0");
		gen_expr(n->r);
		emit("MOV R1, R0");
		emit("POP32 R0");
	}
	emit("CMP32 R0, R1");
}

static void gen_branch(Node *n, int lab, int when) {
	if (n->kind >= N_EQ && n->kind <= N_GE) {
		gen_cmp_operands(n);
		emit("%s .L%d", cmp_branch(n->kind, n->op, !when), lab);
		return;
	}
	if (n->kind == N_NOT) {
		gen_branch(n->l, lab, !when);
		return;
	}
	if (n->kind == N_LOGAND) {
		if (when) {
			int skip = new_label();
			gen_branch(n->l, skip, 0);
			gen_branch(n->r, lab, 1);
			emit(".L%d:", skip);
		} else {
			gen_branch(n->l, lab, 0);
			gen_branch(n->r, lab, 0);
		}
		return;
	}
	if (n->kind == N_LOGOR) {
		if (when) {
			gen_branch(n->l, lab, 1);
			gen_branch(n->r, lab, 1);
		} else {
			int skip = new_label();
			gen_branch(n->l, skip, 1);
			gen_branch(n->r, lab, 0);
			emit(".L%d:", skip);
		}
		return;
	}
	gen_expr(n);
	emit("TEST R0, R0");
	emit("%s .L%d", when ? "BNE" : "BEQ", lab);
}

static void gen_addr(Node *n) {
	switch (n->kind) {
	case N_VAR:
		if (n->sym->kind == S_LOCAL) emit("LEA R0, [R7 + %d]", n->sym->off);
		else emit("LOAD32 R0, %s", sname(n->sym->label));
		return;
	case N_STR:
		emit("LOAD32 R0, %s", n->label);
		return;
	case N_DEREF:
		gen_expr(n->l);
		return;
	case N_CALL:
	case N_ASSIGN:
	case N_COND:
	case N_COMMA:
	case N_VAARG:
		if (is_struct(n->ty)) {
			gen_expr(n);
			return;
		}
		break;
	}
	error_at(n->tok, "not an lvalue");
}

static void gen_call(Node *n) {
	Node *a;
	Node *stack[256];
	int na = 0, i, bytes = 0, direct;
	for (a = n->args; a; a = a->next) {
		if (na >= 256) error_at(n->tok, "too many arguments");
		stack[na++] = a;
	}
	for (i = na - 1; i >= 0; i--) {
		a = stack[i];
		if (is_struct(a->ty)) {
			int sz = (a->ty->size + 3) & ~3;
			gen_expr(a);
			emit("ADDSP %d", -sz);
			emit("MOVSP R1");
			copy_mem(a->ty->size);
			bytes += sz;
		} else {
			gen_expr(a);
			emit("PUSH32 R0");
			bytes += 4;
		}
	}
	if (is_struct(n->ty)) {
		emit("LEA R0, [R7 + %d]", n->tmpoff);
		emit("PUSH32 R0");
		bytes += 4;
	}
	direct = n->l->kind == N_VAR && n->l->ty->kind == TY_FUNC;
	if (direct) {
		emit("CALL32 %s", sname(n->l->sym->label));
	} else {
		gen_expr(n->l);
		emit("CALLR R0");
	}
	if (bytes) emit("ADDSP %d", bytes);
}

static void gen_incdec(Node *n) {
	int post = n->kind == N_POSTINC || n->kind == N_POSTDEC;
	int dec = n->kind == N_PREDEC || n->kind == N_POSTDEC;
	Type *t = n->l->ty;
	gen_addr(n->l);
	emit("MOV R1, R0");
	emit("MOV R2, R0");
	load(t);
	if (post) emit("PUSH32 R0");
	emit("%s R0, %u", dec ? "SUB32" : "ADD32", n->val);
	convert(ty_int, t);
	emit("MOV R1, R2");
	store(t);
	if (post) emit("POP32 R0");
}

static void gen_expr(Node *n) {
	int l1, l2;
	switch (n->kind) {
	case N_NUM:
		emit("LOAD32 R0, %u", n->val);
		return;
	case N_VAR:
		if (n->sym->kind == S_LOCAL && !noload(n->ty)) {
			if (n->ty->kind == TY_CHAR) emit("%s R0, [R7 + %d]", n->ty->uns ? "LDB" : "LDBS", n->sym->off);
			else if (n->ty->kind == TY_SHORT) emit("%s R0, [R7 + %d]", n->ty->uns ? "LDH" : "LDHS", n->sym->off);
			else emit("LDW R0, [R7 + %d]", n->sym->off);
			return;
		}
		gen_addr(n);
		load(n->ty);
		return;
	case N_STR:
		gen_addr(n);
		return;
	case N_DEREF:
		gen_expr(n->l);
		load(n->ty);
		return;
	case N_ADDR:
		gen_addr(n->l);
		return;
	case N_CAST:
		gen_expr(n->l);
		convert(n->l->ty, n->ty);
		return;
	case N_ASSIGN:
		if (is_struct(n->ty)) {
			gen_addr(n->l);
			emit("PUSH32 R0");
			gen_expr(n->r);
			emit("POP32 R1");
			emit("PUSH32 R1");
			copy_mem(n->ty->size);
			emit("POP32 R0");
			return;
		}
		if (n->l->kind == N_VAR && n->l->sym->kind == S_LOCAL) {
			gen_expr(n->r);
			convert(n->r->ty, n->ty);
			emit("LEA R1, [R7 + %d]", n->l->sym->off);
			store(n->ty);
			return;
		}
		gen_addr(n->l);
		emit("PUSH32 R0");
		gen_expr(n->r);
		convert(n->r->ty, n->ty);
		emit("POP32 R1");
		store(n->ty);
		return;
	case N_OPASSIGN:
		gen_addr(n->l);
		emit("PUSH32 R0");
		load(n->l->ty);
		emit("PUSH32 R0");
		gen_expr(n->r);
		emit("MOV R1, R0");
		emit("POP32 R0");
		binop(n->op, n->size);
		convert(ty_int, n->ty);
		emit("POP32 R1");
		store(n->ty);
		return;
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		gen_incdec(n);
		return;
	case N_COND:
		l1 = new_label();
		l2 = new_label();
		gen_branch(n->c, l1, 0);
		gen_expr(n->l);
		if (!is_struct(n->ty)) convert(n->l->ty, n->ty);
		emit("JMPREL .L%d", l2);
		emit(".L%d:", l1);
		gen_expr(n->r);
		if (!is_struct(n->ty)) convert(n->r->ty, n->ty);
		emit(".L%d:", l2);
		return;
	case N_COMMA:
		gen_expr(n->l);
		gen_expr(n->r);
		return;
	case N_CALL:
		gen_call(n);
		return;
	case N_NEG:
		gen_expr(n->l);
		emit("NEG R0");
		return;
	case N_BITNOT:
		gen_expr(n->l);
		emit("XOR32 R0, 0xFFFFFFFF");
		return;
	case N_NOT: case N_LOGAND: case N_LOGOR:
	case N_EQ: case N_NE: case N_LT: case N_LE: case N_GT: case N_GE:
		l1 = new_label();
		l2 = new_label();
		gen_branch(n, l1, 1);
		emit("LOAD32 R0, 0");
		emit("JMPREL .L%d", l2);
		emit(".L%d:", l1);
		emit("LOAD32 R0, 1");
		emit(".L%d:", l2);
		return;
	case N_VASTART: {
		Sym *s = n->r->sym;
		gen_addr(n->l);
		emit("LEA R1, [R7 + %d]", s->off + ((s->ty->size + 3) & ~3));
		emit("STW [R0], R1");
		return;
	}
	case N_VAARG:
		gen_addr(n->l);
		emit("LDW R1, [R0]");
		emit("ADD32 R1, %d", n->size);
		emit("STW [R0], R1");
		emit("MOV R0, R1");
		emit("SUB32 R0, %d", n->size);
		load(n->ty);
		return;
	}
	if (n->kind >= N_ADD && n->kind <= N_SHR) {
		gen_expr(n->l);
		if (simple_rhs(n->r)) {
			gen_r1(n->r);
		} else {
			emit("PUSH32 R0");
			gen_expr(n->r);
			emit("MOV R1, R0");
			emit("POP32 R0");
		}
		binop(n->kind, n->ty->uns);
		return;
	}
	error_at(n->tok, "internal error: cannot generate expression %d", n->kind);
}

static void gen_switch(Node *n) {
	Node *c;
	gen_expr(n->c);
	for (c = n->cases; c; c = c->cnext) {
		emit("CMP32 R0, %u", c->val);
		emit("BEQ .L%d", c->lab);
	}
	emit("JMPREL .L%d", n->deflt ? n->deflt->lab : n->brk);
	gen_stmt(n->body);
	emit(".L%d:", n->brk);
}

static void gen_stmt(Node *n) {
	Node *s;
	int l1, l2;
	if (!n) return;
	switch (n->kind) {
	case N_NULL:
		return;
	case N_BLOCK:
		for (s = n->body; s; s = s->next) gen_stmt(s);
		return;
	case N_EXPR:
		gen_expr(n->l);
		return;
	case N_MEMZERO: {
		int i;
		emit("LOAD32 R0, 0");
		for (i = 0; i + 4 <= n->size; i += 4) emit("STW [R7 + %d], R0", n->sym->off + i);
		for (; i < n->size; i++) emit("STB [R7 + %d], R0", n->sym->off + i);
		return;
	}
	case N_IF:
		l1 = new_label();
		gen_branch(n->c, l1, 0);
		gen_stmt(n->l);
		if (n->r) {
			l2 = new_label();
			emit("JMPREL .L%d", l2);
			emit(".L%d:", l1);
			gen_stmt(n->r);
			emit(".L%d:", l2);
		} else {
			emit(".L%d:", l1);
		}
		return;
	case N_WHILE:
		emit(".L%d:", n->cont);
		gen_branch(n->c, n->brk, 0);
		gen_stmt(n->body);
		emit("JMPREL .L%d", n->cont);
		emit(".L%d:", n->brk);
		return;
	case N_DO:
		l1 = new_label();
		emit(".L%d:", l1);
		gen_stmt(n->body);
		emit(".L%d:", n->cont);
		gen_branch(n->c, l1, 1);
		emit(".L%d:", n->brk);
		return;
	case N_FOR:
		l1 = new_label();
		gen_stmt(n->init);
		emit(".L%d:", l1);
		if (n->c) gen_branch(n->c, n->brk, 0);
		gen_stmt(n->body);
		emit(".L%d:", n->cont);
		if (n->inc) gen_expr(n->inc);
		emit("JMPREL .L%d", l1);
		emit(".L%d:", n->brk);
		return;
	case N_SWITCH:
		gen_switch(n);
		return;
	case N_CASE:
	case N_DEFAULT:
	case N_LABEL:
		emit(".L%d:", n->lab);
		gen_stmt(n->body);
		return;
	case N_BREAK:
	case N_CONTINUE:
	case N_GOTO:
		emit("JMPREL .L%d", n->lab);
		return;
	case N_RETURN:
		if (n->l) {
			gen_expr(n->l);
			if (is_struct(n->l->ty) && !n->op) {
				emit("LDW R1, [R7 + 8]");
				copy_mem(n->l->ty->size);
				emit("LDW R0, [R7 + 8]");
			} else if (!n->op) {
				convert(n->l->ty, gfunc->sym->ty->base);
			}
		}
		emit("JMPREL .L%d", ret_lab);
		return;
	}
	error_at(n->tok, "internal error: cannot generate statement %d", n->kind);
}

static void gen_func(Func *f) {
	gfunc = f;
	ret_lab = new_label();
	emit(".text");
	if (!f->sym->is_static) emit(".globl %s", f->sym->label);
	emit("%s:", f->sym->label);
	emit("PUSH32 R7");
	emit("MOVSP R7");
	if (f->frame) emit("ADDSP %d", -f->frame);
	gen_stmt(f->body);
	if (strcmp(f->sym->name, "main") == 0) emit("LOAD32 R0, 0");
	emit(".L%d:", ret_lab);
	emit("SETSP R7");
	emit("POP32 R7");
	emit("RET32");
}

static void emit_string(char *data, int len) {
	int i = 0;
	while (i < len) {
		char buf[200];
		int k = 0;
		while (i < len && k < 160) {
			unsigned char c = (unsigned char)data[i++];
			if (c == '"' || c == '\\') {
				buf[k++] = '\\';
				buf[k++] = (char)c;
			} else if (c >= 32 && c < 127) {
				buf[k++] = (char)c;
			} else {
				sprintf(buf + k, "\\x%02X", c);
				k += 4;
			}
		}
		buf[k] = 0;
		emit(".ascii \"%s\"", buf);
	}
	emit(".byte 0");
}

static void gen_gvar(GVar *g) {
	Sym *s = g->sym;
	Init *in;
	int pos = 0;
	if (!s->defined && !s->tentative) return;
	if (!g->has_init) {
		emit(".bss");
		if (!s->is_static) emit(".globl %s", s->label);
		emit(".align %d", s->ty->size >= 4 ? 4 : s->ty->align);
		emit("%s:", s->label);
		emit(".space %d", s->ty->size ? s->ty->size : 1);
		return;
	}
	emit(".data");
	if (!s->is_static) emit(".globl %s", s->label);
	emit(".align %d", s->ty->size >= 4 ? 4 : s->ty->align);
	emit("%s:", s->label);
	for (in = g->inits; in; in = in->next) {
		if (in->off < pos) continue;
		if (in->off > pos) emit(".space %d", in->off - pos);
		if (in->label) emit(".dword %s + %d", sname(in->label), (int)in->val);
		else if (in->size == 1) emit(".byte %u", in->val & 0xFF);
		else if (in->size == 2) emit(".word %u", in->val & 0xFFFF);
		else emit(".dword %u", in->val);
		pos = in->off + in->size;
	}
	if (s->ty->size > pos) emit(".space %d", s->ty->size - pos);
}

void gen_program(void) {
	Func *f;
	GVar *g;
	StrLit *s;
	for (f = funcs; f; f = f->next) gen_func(f);
	for (g = gvars; g; g = g->next) gen_gvar(g);
	if (strlits) emit(".data");
	for (s = strlits; s; s = s->next) {
		emit("%s:", s->label);
		emit_string(s->data, s->len);
	}
}
