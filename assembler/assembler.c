/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define NHASH 4096
#define MAXINC 16
#define MAXMACDEPTH 32
#define FLAT_LIMIT 0x10000000u

enum {
	K_N, K_LR, K_INT, K_MOV, K_ALU16, K_A16, K_RI16, K_LOAD, K_STORE, K_LOAD32, K_STORE32,
	K_ALU32, K_R2, K_RA, K_RB, K_RI, K_REL, K_ABS, K_LD, K_ST, K_LEAPC, K_MFCR, K_MTCR
};

enum { O_NONE, O_REG, O_SP, O_CR, O_MEM, O_EXP };

enum { R_ABS32 = 1, R_REL32 = 2, R_ABS16 = 3 };

typedef struct {
	const char *name;
	int kind;
	int op;
	int op2;
} Ins;

static Ins instab[] = {
	{"NOP", K_N, 0x00, 0}, {"HALT", K_N, 0xFF, 0}, {"RET", K_N, 0x0B, 0}, {"CLI", K_N, 0x1A, 0},
	{"STI", K_N, 0x1B, 0}, {"IRET", K_N, 0x19, 0}, {"SYSCALL", K_N, 0x3F, 0}, {"SYSRET", K_N, 0x40, 0},
	{"NOT", K_LR, 0x09, 0}, {"PUSH", K_LR, 0x39, 0}, {"POP", K_LR, 0x3A, 0}, {"INT", K_INT, 0x18, 0},
	{"MOV", K_MOV, 0x30, 0},
	{"ADD", K_ALU16, 0x02, 0x31}, {"SUB", K_ALU16, 0x0E, 0x32}, {"CMP", K_ALU16, 0x0F, 0x33},
	{"AND", K_ALU16, 0x06, 0x34}, {"OR", K_ALU16, 0x07, 0x35}, {"XOR", K_ALU16, 0x08, 0x36},
	{"MUL", K_ALU16, 0x16, 0x37}, {"DIV", K_ALU16, 0x17, 0x38},
	{"JMP", K_A16, 0x04, 0}, {"CALL", K_A16, 0x0A, 0}, {"JC", K_A16, 0x10, 0}, {"JN", K_A16, 0x11, 0},
	{"JO", K_A16, 0x12, 0}, {"JNZ", K_A16, 0x13, 0},
	{"JZ", K_RI16, 0x05, 0}, {"IN", K_RI16, 0x0C, 0}, {"OUT", K_RI16, 0x0D, 0}, {"SHL", K_RI16, 0x14, 0},
	{"SHR", K_RI16, 0x15, 0},
	{"LOAD", K_LOAD, 0x01, 0x3B}, {"STORE", K_STORE, 0x03, 0x3C},
	{"LOAD32", K_LOAD32, 0x20, 0x3D}, {"STORE32", K_STORE32, 0x21, 0x3E},
	{"ADD32", K_ALU32, 0x22, 0x89}, {"SUB32", K_ALU32, 0x23, 0x8A}, {"MUL32", K_ALU32, 0x24, 0x8C},
	{"DIV32", K_ALU32, 0x25, 0x8D}, {"SHL32", K_ALU32, 0x26, 0}, {"SHR32", K_ALU32, 0x27, 0},
	{"AND32", K_ALU32, 0x28, 0x34}, {"OR32", K_ALU32, 0x29, 0x35}, {"XOR32", K_ALU32, 0x2A, 0x36},
	{"CMP32", K_ALU32, 0x2B, 0x8B},
	{"JMPR", K_RB, 0x41, 0}, {"CALLR", K_RB, 0x42, 0}, {"JMPREL", K_REL, 0x43, 0}, {"CALLREL", K_REL, 0x44, 0},
	{"BEQ", K_REL, 0x45, 0}, {"BNE", K_REL, 0x46, 0}, {"BLT", K_REL, 0x47, 0}, {"BGE", K_REL, 0x48, 0},
	{"BLE", K_REL, 0x49, 0}, {"BGT", K_REL, 0x4A, 0}, {"BLO", K_REL, 0x4B, 0}, {"BHS", K_REL, 0x4C, 0},
	{"BLS", K_REL, 0x4D, 0}, {"BHI", K_REL, 0x4E, 0}, {"BMI", K_REL, 0x4F, 0}, {"BPL", K_REL, 0x50, 0},
	{"BVS", K_REL, 0x51, 0}, {"BVC", K_REL, 0x52, 0}, {"JMP32", K_ABS, 0x53, 0}, {"CALL32", K_ABS, 0x54, 0},
	{"ADC", K_R2, 0x55, 0}, {"SBB", K_R2, 0x56, 0}, {"SARV", K_R2, 0x57, 0}, {"SHLV", K_R2, 0x58, 0},
	{"SHRV", K_R2, 0x59, 0}, {"ROLV", K_R2, 0x5A, 0}, {"RORV", K_R2, 0x5B, 0}, {"IMUL", K_R2, 0x5C, 0},
	{"IDIV", K_R2, 0x5D, 0}, {"UMOD", K_R2, 0x5E, 0}, {"IMOD", K_R2, 0x5F, 0}, {"NEG", K_RA, 0x60, 0},
	{"TEST", K_R2, 0x61, 0}, {"MULHU", K_R2, 0x62, 0}, {"MULHS", K_R2, 0x63, 0},
	{"SAR", K_RI, 0x64, 0}, {"ROL", K_RI, 0x65, 0}, {"ROR", K_RI, 0x66, 0}, {"ADCI", K_RI, 0x67, 0},
	{"SBBI", K_RI, 0x68, 0}, {"IMULI", K_RI, 0x69, 0}, {"IDIVI", K_RI, 0x6A, 0}, {"UMODI", K_RI, 0x6B, 0},
	{"IMODI", K_RI, 0x6C, 0}, {"TESTI", K_RI, 0x6D, 0},
	{"LDB", K_LD, 0x6E, 0}, {"LDBS", K_LD, 0x6F, 0}, {"LDH", K_LD, 0x70, 0}, {"LDHS", K_LD, 0x71, 0},
	{"LDW", K_LD, 0x72, 0}, {"STB", K_ST, 0x73, 0}, {"STH", K_ST, 0x74, 0}, {"STW", K_ST, 0x75, 0},
	{"LEA", K_LD, 0x76, 0}, {"LEAPC", K_LEAPC, 0x77, 0}, {"MOVSP", K_RA, 0x78, 0}, {"SETSP", K_RB, 0x79, 0},
	{"ADDSP", K_ABS, 0x7A, 0}, {"BRK", K_N, 0x7C, 0}, {"MFCR", K_MFCR, 0x7D, 0}, {"MTCR", K_MTCR, 0x7E, 0},
	{"TLBFLUSH", K_N, 0x7F, 0}, {"INVLPG", K_RB, 0x80, 0}, {"WFI", K_N, 0x81, 0}, {"CPUID", K_RA, 0x82, 0},
	{"PUSHF", K_N, 0x83, 0}, {"POPF", K_N, 0x84, 0}, {"RDCYC", K_RA, 0x85, 0}, {"RET32", K_N, 0x86, 0},
	{"PUSH32", K_RB, 0x87, 0}, {"POP32", K_RA, 0x88, 0},
	{NULL, 0, 0, 0}
};

typedef struct {
	char *name;
	unsigned val;
	int sec;
	int glob;
	int defined;
	int rsym;
	int used;
	int oidx;
	int next;
} Sym;

typedef struct {
	unsigned v;
	int s;
} Val;

typedef struct {
	int kind;
	int reg;
	Val v;
} Opnd;

typedef struct {
	char *text;
	int file;
	int line;
} Line;

typedef struct {
	char *name;
	int nargs;
	char *args[16];
	char **body;
	int nbody;
} Macro;

typedef struct {
	unsigned off;
	int sec;
	int type;
	int sym;
	unsigned add;
} Reloc;

static Sym *syms;
static int nsyms, maxsyms;
static int hasht[NHASH];
static Line *lines;
static int nlines, maxlines;
static char *fnames[256];
static int nfiles;
static Macro *macros;
static int nmacros, maxmacros;
static Reloc *relocs;
static int nrelocs, maxrelocs;
static char *incdirs[16];
static int nincdirs;

static int objmode;
static int pass;
static int cur_file, cur_line;
static int cursec;
static unsigned loc[4];
static unsigned secsize[4];
static unsigned char *secbuf[4];
static unsigned secmax[4];
static unsigned origin;
static int emitted;
static unsigned flatmax;
static char lastglobal[256];
static int uniq;

static void fatal(const char *fmt, ...) {
	va_list ap;
	if (cur_file >= 0 && cur_file < nfiles) fprintf(stderr, "%s:%d: error: ", fnames[cur_file], cur_line);
	else fprintf(stderr, "as: error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

static void *xrealloc(void *p, unsigned n) {
	p = realloc(p, n);
	if (!p) {
		fprintf(stderr, "as: out of memory\n");
		exit(1);
	}
	return p;
}

static char *xstrdup(const char *s) {
	char *d = xrealloc(NULL, strlen(s) + 1);
	strcpy(d, s);
	return d;
}

static int ieq(const char *a, const char *b) {
	while (*a && *b) {
		if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
		a++;
		b++;
	}
	return *a == *b;
}

static unsigned hashs(const char *s) {
	unsigned h = 5381;
	while (*s) h = h * 33 + (unsigned char)*s++;
	return h % NHASH;
}

static int sym_find(const char *name) {
	int i = hasht[hashs(name)];
	while (i >= 0) {
		if (strcmp(syms[i].name, name) == 0) return i;
		i = syms[i].next;
	}
	return -1;
}

static int sym_get(const char *name) {
	int i = sym_find(name);
	unsigned h;
	if (i >= 0) return i;
	if (nsyms == maxsyms) {
		maxsyms = maxsyms ? maxsyms * 2 : 1024;
		syms = xrealloc(syms, maxsyms * sizeof(Sym));
	}
	i = nsyms++;
	memset(&syms[i], 0, sizeof(Sym));
	syms[i].name = xstrdup(name);
	syms[i].rsym = -1;
	syms[i].oidx = -1;
	h = hashs(name);
	syms[i].next = hasht[h];
	hasht[h] = i;
	return i;
}

static void full_name(const char *name, char *out) {
	if (name[0] == '.' && name[1]) {
		if (strlen(lastglobal) + strlen(name) > 250) fatal("label name too long");
		strcpy(out, lastglobal);
		strcat(out, name);
	} else {
		if (strlen(name) > 250) fatal("label name too long");
		strcpy(out, name);
	}
}

static void add_line(const char *text) {
	if (nlines == maxlines) {
		maxlines = maxlines ? maxlines * 2 : 4096;
		lines = xrealloc(lines, maxlines * sizeof(Line));
	}
	lines[nlines].text = xstrdup(text);
	lines[nlines].file = cur_file;
	lines[nlines].line = cur_line;
	nlines++;
}

static int is_id0(int c) {
	return isalpha(c) || c == '_' || c == '.' || c == '$';
}

static int is_id(int c) {
	return isalnum(c) || c == '_' || c == '.' || c == '$';
}

static char *skipws(char *p) {
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	return p;
}

static void strip_comment(char *s) {
	char q = 0;
	while (*s) {
		if (q) {
			if (*s == '\\' && s[1]) s++;
			else if (*s == q) q = 0;
		} else if (*s == '"' || *s == '\'') {
			q = *s;
		} else if (*s == ';') {
			*s = 0;
			return;
		}
		s++;
	}
}

static void rtrim(char *s) {
	int n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static int split_args(char *s, char **out, int max) {
	int n = 0, depth = 0;
	char q = 0;
	char *start;
	s = skipws(s);
	if (!*s) return 0;
	start = s;
	while (1) {
		if (q) {
			if (*s == '\\' && s[1]) s++;
			else if (*s == q) q = 0;
		} else if (*s == '"' || *s == '\'') {
			q = *s;
		} else if (*s == '(' || *s == '[') {
			depth++;
		} else if (*s == ')' || *s == ']') {
			depth--;
		} else if ((*s == ',' && depth == 0) || *s == 0) {
			int end = *s == 0;
			*s = 0;
			if (n == max) fatal("too many operands");
			start = skipws(start);
			rtrim(start);
			out[n++] = start;
			if (end) break;
			start = s + 1;
		}
		s++;
	}
	return n;
}

static int find_macro(const char *name) {
	int i;
	for (i = 0; i < nmacros; i++)
		if (strcmp(macros[i].name, name) == 0) return i;
	return -1;
}

static void process_line(char *text, int depth);
static void read_file(const char *path, int depth);

static Macro *defmac;

static void expand_macro(int mi, char *argstr, int depth) {
	Macro *m = &macros[mi];
	char *args[16];
	char buf[4096], tmp[4096];
	int n, i, j, id;
	if (depth > MAXMACDEPTH) fatal("macro expansion too deep");
	strcpy(tmp, argstr);
	n = split_args(tmp, args, 16);
	if (n > m->nargs) fatal("too many arguments for macro %s", m->name);
	id = uniq++;
	for (i = 0; i < m->nbody; i++) {
		char *s = m->body[i];
		int o = 0;
		while (*s) {
			if (*s == '\\' && s[1] == '@') {
				o += sprintf(buf + o, "%d", id);
				s += 2;
				continue;
			}
			if (*s == '\\' && is_id0((unsigned char)s[1])) {
				char nm[64];
				int k = 0, found = -1;
				const char *t = s + 1;
				while (is_id((unsigned char)*t) && *t != '.' && k < 63) nm[k++] = *t++;
				nm[k] = 0;
				for (j = 0; j < m->nargs; j++)
					if (strcmp(m->args[j], nm) == 0) found = j;
				if (found >= 0) {
					const char *a = found < n ? args[found] : "";
					if (o + strlen(a) >= sizeof(buf) - 1) fatal("macro line too long");
					strcpy(buf + o, a);
					o += strlen(a);
					s = (char *)t;
					continue;
				}
			}
			if (o >= (int)sizeof(buf) - 2) fatal("macro line too long");
			buf[o++] = *s++;
		}
		buf[o] = 0;
		process_line(buf, depth + 1);
	}
}

static void process_line(char *text, int depth) {
	char buf[4096];
	char *p, *q;
	char word[256];
	int k, mi;
	if (strlen(text) >= sizeof(buf)) fatal("line too long");
	strcpy(buf, text);
	strip_comment(buf);
	rtrim(buf);
	p = skipws(buf);
	if (defmac) {
		k = 0;
		q = p;
		while (is_id((unsigned char)*q) && k < 255) word[k++] = *q++;
		word[k] = 0;
		if (ieq(word, ".endm")) {
			defmac = NULL;
			return;
		}
		if (ieq(word, ".macro")) fatal("nested macro definition");
		defmac->body = xrealloc(defmac->body, (defmac->nbody + 1) * sizeof(char *));
		defmac->body[defmac->nbody++] = xstrdup(buf);
		return;
	}
	if (!*p) return;
	q = p;
	if (is_id0((unsigned char)*q)) {
		char *e = q;
		while (is_id((unsigned char)*e)) e++;
		char *c = skipws(e);
		if (*c == ':') {
			char save = *e;
			*e = 0;
			sprintf(word, "%s:", q);
			*e = save;
			add_line(word);
			p = skipws(c + 1);
			if (!*p) return;
		}
	}
	k = 0;
	q = p;
	while (is_id((unsigned char)*q) && k < 255) word[k++] = *q++;
	word[k] = 0;
	if (ieq(word, ".macro")) {
		char *args[17];
		int n, i;
		char *nm = skipws(q);
		char *e = nm;
		if (!is_id0((unsigned char)*e)) fatal("macro name expected");
		while (is_id((unsigned char)*e)) e++;
		if (nmacros == maxmacros) {
			maxmacros = maxmacros ? maxmacros * 2 : 64;
			macros = xrealloc(macros, maxmacros * sizeof(Macro));
		}
		defmac = &macros[nmacros++];
		memset(defmac, 0, sizeof(Macro));
		if (*e) *e++ = 0;
		defmac->name = xstrdup(nm);
		n = split_args(e, args, 17);
		if (n > 16) fatal("too many macro parameters");
		for (i = 0; i < n; i++) {
			if (!is_id0((unsigned char)args[i][0])) fatal("bad macro parameter '%s'", args[i]);
			defmac->args[i] = xstrdup(args[i]);
		}
		defmac->nargs = n;
		return;
	}
	if (ieq(word, ".endm")) fatal(".endm without .macro");
	if (ieq(word, ".include")) {
		char path[1024];
		char *s = skipws(q);
		char *e;
		if (*s != '"') fatal(".include needs a quoted file name");
		e = strchr(s + 1, '"');
		if (!e) fatal("unterminated file name");
		*e = 0;
		if (depth >= MAXINC) fatal("include nested too deeply");
		strcpy(path, s + 1);
		read_file(path, depth + 1);
		return;
	}
	mi = find_macro(word);
	if (mi >= 0) {
		expand_macro(mi, q, depth);
		return;
	}
	add_line(p);
}

static FILE *open_inc(const char *path, char *found) {
	FILE *f;
	int i;
	char buf[1024];
	if (path[0] != '/' && cur_file >= 0) {
		const char *cf = fnames[cur_file];
		const char *sl = strrchr(cf, '/');
		if (sl) {
			int n = sl - cf + 1;
			memcpy(buf, cf, n);
			strcpy(buf + n, path);
			f = fopen(buf, "r");
			if (f) {
				strcpy(found, buf);
				return f;
			}
		}
	}
	f = fopen(path, "r");
	if (f) {
		strcpy(found, path);
		return f;
	}
	for (i = 0; i < nincdirs; i++) {
		sprintf(buf, "%s/%s", incdirs[i], path);
		f = fopen(buf, "r");
		if (f) {
			strcpy(found, buf);
			return f;
		}
	}
	return NULL;
}

static void read_file(const char *path, int depth) {
	char found[1024];
	char buf[4096];
	FILE *f = open_inc(path, found);
	int sf = cur_file, sl = cur_line, fi;
	if (!f) {
		if (cur_file >= 0) fatal("cannot open include file '%s'", path);
		fprintf(stderr, "as: cannot open '%s'\n", path);
		exit(1);
	}
	if (nfiles == 256) fatal("too many files");
	fi = nfiles++;
	fnames[fi] = xstrdup(found);
	cur_file = fi;
	cur_line = 0;
	while (fgets(buf, sizeof(buf), f)) {
		cur_line++;
		if (!strchr(buf, '\n') && !feof(f)) fatal("line too long");
		process_line(buf, depth);
	}
	fclose(f);
	if (depth == 0 && defmac) fatal("missing .endm");
	cur_file = sf;
	cur_line = sl;
}

static Val mkval(unsigned v, int s) {
	Val r;
	r.v = v;
	r.s = s;
	return r;
}

static char *ep;

static Val e_or(void);

static int esc_char(char **pp) {
	char *p = *pp;
	int c = (unsigned char)*p++;
	if (c == '\\') {
		c = (unsigned char)*p++;
		switch (c) {
		case 'n': c = '\n'; break;
		case 't': c = '\t'; break;
		case 'r': c = '\r'; break;
		case 'b': c = 8; break;
		case 'a': c = 7; break;
		case 'f': c = 12; break;
		case 'v': c = 11; break;
		case '0': c = 0; break;
		case '\\': c = '\\'; break;
		case '\'': c = '\''; break;
		case '"': c = '"'; break;
		case 'x': {
			int v = 0, k = 0;
			while (k < 2 && isxdigit((unsigned char)*p)) {
				v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10));
				p++;
				k++;
			}
			c = v;
			break;
		}
		default: fatal("unknown escape '\\%c'", c);
		}
	} else if (c == 0) {
		fatal("unterminated literal");
	}
	*pp = p;
	return c;
}

static Val sym_val(const char *raw) {
	char name[256];
	int i;
	Sym *s;
	full_name(raw, name);
	i = sym_get(name);
	s = &syms[i];
	if (!s->defined) {
		if (pass == 1) return mkval(0, -1);
		if (!objmode) fatal("undefined symbol '%s'", name);
		s->used = 1;
		return mkval(0, i);
	}
	if (s->sec == 4) return mkval(s->val, s->rsym);
	if (!objmode) return mkval(s->val, -1);
	return mkval(s->val, s->sec - 1);
}

static Val e_primary(void) {
	Val v;
	ep = skipws(ep);
	if (*ep == '(') {
		ep++;
		v = e_or();
		ep = skipws(ep);
		if (*ep != ')') fatal("missing ')'");
		ep++;
		return v;
	}
	if (*ep == '\'') {
		int c;
		ep++;
		c = esc_char(&ep);
		if (*ep != '\'') fatal("bad character literal");
		ep++;
		return mkval(c, -1);
	}
	if (isdigit((unsigned char)*ep)) {
		unsigned x = 0;
		int base = 10;
		if (ep[0] == '0' && (ep[1] == 'x' || ep[1] == 'X')) {
			base = 16;
			ep += 2;
			if (!isxdigit((unsigned char)*ep)) fatal("bad hex number");
		} else if (ep[0] == '0' && (ep[1] == 'b' || ep[1] == 'B') && (ep[2] == '0' || ep[2] == '1')) {
			base = 2;
			ep += 2;
		}
		while (isalnum((unsigned char)*ep)) {
			int d;
			int c = tolower((unsigned char)*ep);
			if (isdigit(c)) d = c - '0';
			else d = c - 'a' + 10;
			if (d >= base) fatal("bad digit '%c' in number", *ep);
			x = x * base + d;
			ep++;
		}
		return mkval(x, -1);
	}
	if (*ep == '.' && !is_id((unsigned char)ep[1])) {
		ep++;
		if (!objmode) return mkval(loc[cursec], -1);
		return mkval(loc[cursec], cursec - 1);
	}
	if (is_id0((unsigned char)*ep)) {
		char name[256];
		int k = 0;
		while (is_id((unsigned char)*ep)) {
			if (k >= 250) fatal("symbol name too long");
			name[k++] = *ep++;
		}
		name[k] = 0;
		return sym_val(name);
	}
	if (*ep == 0) fatal("expression expected");
	fatal("unexpected '%c' in expression", *ep);
	return mkval(0, -1);
}

static void need_abs(Val a, Val b) {
	if ((a.s >= 0 || b.s >= 0) && pass == 2) fatal("relocatable value not allowed with this operator");
}

static Val e_unary(void) {
	Val v;
	ep = skipws(ep);
	if (*ep == '-') {
		ep++;
		v = e_unary();
		need_abs(v, mkval(0, -1));
		return mkval(-v.v, -1);
	}
	if (*ep == '~') {
		ep++;
		v = e_unary();
		need_abs(v, mkval(0, -1));
		return mkval(~v.v, -1);
	}
	if (*ep == '+') {
		ep++;
		return e_unary();
	}
	return e_primary();
}

static Val e_mul(void) {
	Val a = e_unary(), b;
	while (1) {
		char c;
		ep = skipws(ep);
		c = *ep;
		if (c != '*' && c != '/' && c != '%') return a;
		ep++;
		b = e_unary();
		need_abs(a, b);
		if (c == '*') a.v = a.v * b.v;
		else {
			if (b.v == 0) {
				if (pass == 2) fatal("division by zero in expression");
				a.v = 0;
			} else if (c == '/') a.v = (unsigned)((int)a.v / (int)b.v);
			else a.v = (unsigned)((int)a.v % (int)b.v);
		}
		a.s = -1;
	}
}

static Val e_add(void) {
	Val a = e_mul(), b;
	while (1) {
		char c;
		ep = skipws(ep);
		c = *ep;
		if (c != '+' && c != '-') return a;
		ep++;
		b = e_mul();
		if (c == '+') {
			if (a.s >= 0 && b.s >= 0) {
				if (pass == 2) fatal("cannot add two relocatable values");
				a.s = -1;
			}
			a.v = a.v + b.v;
			if (b.s >= 0) a.s = b.s;
		} else {
			if (b.s >= 0) {
				if (a.s == b.s && b.s <= 2 && objmode) a.s = -1;
				else if (pass == 2) fatal("invalid difference of relocatable values");
				else a.s = -1;
			}
			a.v = a.v - b.v;
		}
	}
}

static Val e_shift(void) {
	Val a = e_add(), b;
	while (1) {
		ep = skipws(ep);
		if (!((ep[0] == '<' && ep[1] == '<') || (ep[0] == '>' && ep[1] == '>'))) return a;
		char c = *ep;
		ep += 2;
		b = e_add();
		need_abs(a, b);
		if (c == '<') a.v = b.v >= 32 ? 0 : a.v << b.v;
		else a.v = b.v >= 32 ? 0 : a.v >> b.v;
		a.s = -1;
	}
}

static Val e_and(void) {
	Val a = e_shift(), b;
	while (1) {
		ep = skipws(ep);
		if (*ep != '&') return a;
		ep++;
		b = e_shift();
		need_abs(a, b);
		a.v &= b.v;
		a.s = -1;
	}
}

static Val e_xor(void) {
	Val a = e_and(), b;
	while (1) {
		ep = skipws(ep);
		if (*ep != '^') return a;
		ep++;
		b = e_and();
		need_abs(a, b);
		a.v ^= b.v;
		a.s = -1;
	}
}

static Val e_or(void) {
	Val a = e_xor(), b;
	while (1) {
		ep = skipws(ep);
		if (*ep != '|') return a;
		ep++;
		b = e_xor();
		need_abs(a, b);
		a.v |= b.v;
		a.s = -1;
	}
}

static Val eval(char *s) {
	Val v;
	ep = s;
	v = e_or();
	ep = skipws(ep);
	if (*ep) fatal("junk '%s' after expression", ep);
	return v;
}

static Val eval_abs(char *s) {
	Val v = eval(s);
	if (v.s >= 0) fatal("absolute expression required");
	return v;
}

static int parse_reg(const char *s) {
	if ((s[0] == 'R' || s[0] == 'r') && s[1] >= '0' && s[1] <= '7' && s[2] == 0) return s[1] - '0';
	return -1;
}

static int parse_cr(const char *s) {
	int n;
	if (!((s[0] == 'C' || s[0] == 'c') && (s[1] == 'R' || s[1] == 'r') && isdigit((unsigned char)s[2]))) return -1;
	n = s[2] - '0';
	if (s[3]) {
		if (!isdigit((unsigned char)s[3]) || s[4]) return -1;
		n = n * 10 + s[3] - '0';
	}
	return n > 15 ? -1 : n;
}

static Opnd parse_opnd(char *s) {
	Opnd o;
	int r;
	o.kind = O_EXP;
	o.reg = 0;
	o.v = mkval(0, -1);
	if ((r = parse_reg(s)) >= 0) {
		o.kind = O_REG;
		o.reg = r;
		return o;
	}
	if (ieq(s, "SP")) {
		o.kind = O_SP;
		o.reg = 8;
		return o;
	}
	if ((r = parse_cr(s)) >= 0) {
		o.kind = O_CR;
		o.reg = r;
		return o;
	}
	if (s[0] == '[') {
		char rn[16];
		int k = 0, n = strlen(s);
		char *p;
		if (s[n - 1] != ']') fatal("missing ']'");
		s[n - 1] = 0;
		p = skipws(s + 1);
		while (isalnum((unsigned char)*p) && k < 15) rn[k++] = *p++;
		rn[k] = 0;
		if (ieq(rn, "SP")) r = 8;
		else r = parse_reg(rn);
		if (r < 0) fatal("base register expected in memory operand");
		o.kind = O_MEM;
		o.reg = r;
		p = skipws(p);
		if (*p == '+') {
			o.v = eval(p + 1);
		} else if (*p == '-') {
			o.v = eval(p + 1);
			if (o.v.s >= 0 && pass == 2) fatal("cannot negate a relocatable offset");
			o.v.v = -o.v.v;
			o.v.s = -1;
		} else if (*p) {
			fatal("bad memory operand");
		}
		return o;
	}
	o.v = eval(s);
	return o;
}

static void sec_put(unsigned char b) {
	int s = cursec;
	if (pass == 2) {
		unsigned idx;
		if (s == 3) fatal("data not allowed in .bss");
		idx = objmode ? loc[s] : loc[s] - origin;
		if (idx >= secmax[s]) {
			unsigned nm = secmax[s] ? secmax[s] : 65536;
			while (nm <= idx) nm *= 2;
			if (nm > 0x4000000) fatal("output too large");
			secbuf[s] = xrealloc(secbuf[s], nm);
			memset(secbuf[s] + secmax[s], 0, nm - secmax[s]);
			secmax[s] = nm;
		}
		secbuf[s][idx] = b;
	}
	loc[s]++;
	emitted = 1;
}

static void put8(unsigned v) {
	sec_put(v & 0xFF);
}

static void put16(unsigned v) {
	put8(v);
	put8(v >> 8);
}

static void put32(unsigned v) {
	put16(v);
	put16(v >> 16);
}

static void add_reloc(int type, Val v) {
	if (nrelocs == maxrelocs) {
		maxrelocs = maxrelocs ? maxrelocs * 2 : 1024;
		relocs = xrealloc(relocs, maxrelocs * sizeof(Reloc));
	}
	relocs[nrelocs].off = loc[cursec];
	relocs[nrelocs].sec = cursec;
	relocs[nrelocs].type = type;
	relocs[nrelocs].sym = v.s;
	relocs[nrelocs].add = v.v;
	nrelocs++;
}

static void val8(Val v) {
	if (v.s >= 0 && pass == 2) fatal("relocatable value does not fit in 8 bits");
	put8(v.v);
}

static void val16(Val v) {
	if (v.s >= 0 && pass == 2) {
		add_reloc(R_ABS16, v);
		put16(0);
		return;
	}
	put16(v.v);
}

static void val32(Val v) {
	if (v.s >= 0 && pass == 2) {
		add_reloc(R_ABS32, v);
		put32(0);
		return;
	}
	put32(v.v);
}

static void rel32(Val t) {
	unsigned next = loc[cursec] + 4;
	if (pass == 1) {
		put32(0);
		return;
	}
	if (!objmode) {
		put32(t.v - next);
		return;
	}
	if (t.s == cursec - 1) {
		put32(t.v - next);
		return;
	}
	if (t.s < 0) fatal("branch to an absolute address is not allowed in an object file");
	add_reloc(R_REL32, t);
	put32(0);
}

static void need_n(int n, int want, const char *mn) {
	if (n != want) fatal("%s expects %d operand%s", mn, want, want == 1 ? "" : "s");
}

static int want_reg(Opnd o, const char *mn) {
	if (o.kind != O_REG) fatal("%s: register R0 to R7 expected", mn);
	return o.reg;
}

static void need_exp(Opnd o, const char *mn) {
	if (o.kind != O_EXP) fatal("%s: expression expected", mn);
}

static void need_legacy_mem(Opnd o, const char *mn) {
	if (o.kind != O_MEM) fatal("%s: memory operand expected", mn);
	if (o.reg == 8) fatal("%s: SP cannot be used as base register here", mn);
}

static void instr(Ins *in, char **ops, int n) {
	Opnd a, b;
	const char *mn = in->name;
	int r;
	a.kind = b.kind = O_NONE;
	if (n > 2) fatal("%s: too many operands", mn);
	if (n > 0) a = parse_opnd(ops[0]);
	if (n > 1) b = parse_opnd(ops[1]);
	switch (in->kind) {
	case K_N:
		need_n(n, 0, mn);
		put8(in->op);
		break;
	case K_LR:
		need_n(n, 1, mn);
		r = want_reg(a, mn);
		put8(in->op);
		put8(r);
		break;
	case K_INT:
		need_n(n, 1, mn);
		need_exp(a, mn);
		put8(in->op);
		val8(a.v);
		break;
	case K_MOV:
		need_n(n, 2, mn);
		put8(in->op);
		r = want_reg(a, mn);
		put8(r << 4 | want_reg(b, mn));
		break;
	case K_ALU16:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		if (b.kind == O_REG) {
			put8(in->op2);
			put8(r << 4 | b.reg);
		} else {
			need_exp(b, mn);
			put8(in->op);
			put8(r);
			val16(b.v);
		}
		break;
	case K_A16:
		need_n(n, 1, mn);
		need_exp(a, mn);
		put8(in->op);
		val16(a.v);
		break;
	case K_RI16:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		need_exp(b, mn);
		put8(in->op);
		put8(r);
		val16(b.v);
		break;
	case K_LOAD:
	case K_LOAD32:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		if (b.kind == O_MEM) {
			need_legacy_mem(b, mn);
			put8(in->op2);
			put8(r << 4 | b.reg);
			val16(b.v);
		} else {
			need_exp(b, mn);
			put8(in->op);
			put8(r);
			if (in->kind == K_LOAD) val16(b.v);
			else val32(b.v);
		}
		break;
	case K_STORE:
	case K_STORE32:
		need_n(n, 2, mn);
		if (a.kind == O_MEM) {
			need_legacy_mem(a, mn);
			put8(in->op2);
			put8(a.reg << 4 | want_reg(b, mn));
			val16(a.v);
		} else {
			r = want_reg(a, mn);
			need_exp(b, mn);
			put8(in->op);
			put8(r);
			if (in->kind == K_STORE) val16(b.v);
			else val32(b.v);
		}
		break;
	case K_ALU32:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		if (b.kind == O_REG) {
			if (!in->op2) fatal("%s: register operand not allowed", mn);
			put8(in->op2);
			put8(r << 4 | b.reg);
		} else {
			need_exp(b, mn);
			put8(in->op);
			put8(r);
			val32(b.v);
		}
		break;
	case K_R2:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		put8(in->op);
		put8(r << 4 | want_reg(b, mn));
		break;
	case K_RA:
		need_n(n, 1, mn);
		r = want_reg(a, mn);
		put8(in->op);
		put8(r << 4);
		break;
	case K_RB:
		need_n(n, 1, mn);
		r = want_reg(a, mn);
		put8(in->op);
		put8(r);
		break;
	case K_RI:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		need_exp(b, mn);
		put8(in->op);
		put8(r << 4);
		val32(b.v);
		break;
	case K_REL:
		need_n(n, 1, mn);
		need_exp(a, mn);
		put8(in->op);
		put8(0);
		rel32(a.v);
		break;
	case K_ABS:
		need_n(n, 1, mn);
		need_exp(a, mn);
		put8(in->op);
		put8(0);
		val32(a.v);
		break;
	case K_LEAPC:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		need_exp(b, mn);
		put8(in->op);
		put8(r << 4);
		rel32(b.v);
		break;
	case K_LD:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		if (b.kind != O_MEM) fatal("%s: memory operand expected", mn);
		put8(in->op);
		put8(r << 4 | b.reg);
		val32(b.v);
		break;
	case K_ST:
		need_n(n, 2, mn);
		if (a.kind != O_MEM) fatal("%s: memory operand expected", mn);
		r = want_reg(b, mn);
		put8(in->op);
		put8(r << 4 | a.reg);
		val32(a.v);
		break;
	case K_MFCR:
		need_n(n, 2, mn);
		r = want_reg(a, mn);
		if (b.kind != O_CR) fatal("%s: control register expected", mn);
		put8(in->op);
		put8(r << 4 | b.reg);
		break;
	case K_MTCR:
		need_n(n, 2, mn);
		if (a.kind != O_CR) fatal("%s: control register expected", mn);
		r = want_reg(b, mn);
		put8(in->op);
		put8(a.reg << 4 | r);
		break;
	}
}

static Ins *find_ins(const char *mn) {
	Ins *in;
	for (in = instab; in->name; in++)
		if (ieq(in->name, mn)) return in;
	return NULL;
}

static void define_label(const char *raw) {
	char name[256];
	int i;
	Sym *s;
	if (raw[0] != '.') {
		if (strlen(raw) > 200) fatal("label name too long");
		strcpy(lastglobal, raw);
	}
	full_name(raw, name);
	i = sym_get(name);
	s = &syms[i];
	if (pass == 1) {
		if (s->defined) fatal("duplicate symbol '%s'", name);
		s->defined = 1;
		s->sec = objmode ? cursec : 1;
		s->val = loc[cursec];
	} else if (s->val != loc[cursec] || (objmode && s->sec != cursec)) {
		fatal("internal error: label '%s' moved between passes", name);
	}
}

static void parse_string(char *s, int z) {
	char *p = skipws(s);
	if (*p != '"') fatal("string expected");
	p++;
	while (*p != '"') {
		if (!*p) fatal("unterminated string");
		put8(esc_char(&p));
	}
	p = skipws(p + 1);
	if (*p) fatal("junk after string");
	if (z) put8(0);
}

static void directive(char *dir, char *rest) {
	char *ops[1024];
	int n, i;
	Val v;
	if (ieq(dir, ".org")) {
		if (objmode) fatal(".org is not allowed in an object file");
		v = eval_abs(rest);
		if (!emitted && origin == 0 && loc[1] == 0 && v.v >= FLAT_LIMIT) {
			origin = v.v;
		} else if (v.v < origin) {
			fatal(".org below the start of the output");
		}
		loc[1] = v.v;
	} else if (ieq(dir, ".byte")) {
		n = split_args(rest, ops, 1024);
		for (i = 0; i < n; i++) {
			if (ops[i][0] == '"') parse_string(ops[i], 0);
			else val8(eval(ops[i]));
		}
	} else if (ieq(dir, ".word")) {
		n = split_args(rest, ops, 1024);
		for (i = 0; i < n; i++) val16(eval(ops[i]));
	} else if (ieq(dir, ".dword") || ieq(dir, ".long")) {
		n = split_args(rest, ops, 1024);
		for (i = 0; i < n; i++) val32(eval(ops[i]));
	} else if (ieq(dir, ".ascii")) {
		parse_string(rest, 0);
	} else if (ieq(dir, ".asciiz")) {
		parse_string(rest, 1);
	} else if (ieq(dir, ".equ") || ieq(dir, ".set")) {
		char name[256];
		Sym *s;
		int si;
		n = split_args(rest, ops, 2);
		if (n != 2 || !is_id0((unsigned char)ops[0][0])) fatal(".equ needs a name and a value");
		full_name(ops[0], name);
		v = eval(ops[1]);
		si = sym_get(name);
		s = &syms[si];
		if (pass == 1 && s->defined) fatal("duplicate symbol '%s'", name);
		s->defined = 1;
		s->sec = 4;
		s->val = v.v;
		s->rsym = v.s;
	} else if (ieq(dir, ".align")) {
		v = eval_abs(rest);
		if (v.v == 0 || (v.v & (v.v - 1))) fatal(".align needs a power of two");
		while (loc[cursec] % v.v) {
			if (cursec == 3) loc[3]++;
			else put8(0);
		}
	} else if (ieq(dir, ".space") || ieq(dir, ".zero")) {
		unsigned cnt, fill = 0, k;
		n = split_args(rest, ops, 2);
		if (n < 1) fatal(".space needs a size");
		cnt = eval_abs(ops[0]).v;
		if (n > 1) fill = eval_abs(ops[1]).v;
		if (cnt > 0x4000000) fatal(".space too large");
		if (cursec == 3) {
			if (fill) fatal("fill value not allowed in .bss");
			loc[3] += cnt;
		} else {
			for (k = 0; k < cnt; k++) put8(fill);
		}
	} else if (ieq(dir, ".globl") || ieq(dir, ".global")) {
		n = split_args(rest, ops, 1024);
		for (i = 0; i < n; i++) {
			char name[256];
			if (!is_id0((unsigned char)ops[i][0])) fatal("symbol name expected");
			full_name(ops[i], name);
			syms[sym_get(name)].glob = 1;
		}
	} else if (ieq(dir, ".text") || ieq(dir, ".data") || ieq(dir, ".bss")) {
		if (!objmode) fatal("section directives need -c");
		if (*skipws(rest)) fatal("junk after %s", dir);
		cursec = ieq(dir, ".text") ? 1 : ieq(dir, ".data") ? 2 : 3;
	} else {
		fatal("unknown directive %s", dir);
	}
}

static void statement(char *text) {
	char buf[4096];
	char word[256];
	char *p, *ops[3];
	int k, n;
	Ins *in;
	strcpy(buf, text);
	p = skipws(buf);
	k = strlen(p);
	if (k > 0 && p[k - 1] == ':' && is_id0((unsigned char)p[0])) {
		p[k - 1] = 0;
		define_label(p);
		return;
	}
	k = 0;
	while (is_id((unsigned char)*p) && k < 255) word[k++] = *p++;
	word[k] = 0;
	if (k == 0) fatal("syntax error");
	if (*p && *p != ' ' && *p != '\t') fatal("syntax error near '%s'", p);
	if (word[0] == '.') {
		directive(word, p);
		return;
	}
	in = find_ins(word);
	if (!in) fatal("unknown instruction '%s'", word);
	n = split_args(p, ops, 3);
	instr(in, ops, n);
}

static void run_pass(int ps) {
	int i;
	pass = ps;
	for (i = 1; i < 4; i++) loc[i] = 0;
	cursec = 1;
	origin = ps == 2 ? origin : 0;
	if (ps == 2) loc[1] = origin;
	emitted = 0;
	lastglobal[0] = 0;
	flatmax = loc[1];
	for (i = 0; i < nlines; i++) {
		cur_file = lines[i].file;
		cur_line = lines[i].line;
		statement(lines[i].text);
		if (loc[1] > flatmax) flatmax = loc[1];
	}
	for (i = 1; i < 4; i++)
		if (loc[i] > secsize[i]) secsize[i] = loc[i];
}

static void w32(FILE *f, unsigned v) {
	fputc(v & 0xFF, f);
	fputc((v >> 8) & 0xFF, f);
	fputc((v >> 16) & 0xFF, f);
	fputc((v >> 24) & 0xFF, f);
}

static void write_obj(const char *out) {
	FILE *f = fopen(out, "wb");
	int i, no = 0;
	unsigned strsz = 1;
	int *order;
	if (!f) {
		fprintf(stderr, "as: cannot create '%s'\n", out);
		exit(1);
	}
	order = xrealloc(NULL, (nsyms + 1) * sizeof(int));
	for (i = 0; i < nsyms; i++) {
		Sym *s = &syms[i];
		int keep = i < 3;
		if (i >= 3) {
			if (s->defined && s->sec != 4) keep = 1;
			if (s->defined && s->sec == 4 && s->glob && s->rsym < 0) keep = 1;
			if (!s->defined && (s->used || s->glob)) keep = 1;
		}
		if (keep) {
			s->oidx = no;
			order[no++] = i;
			strsz += strlen(s->name) + 1;
		}
	}
	for (i = 0; i < nrelocs; i++)
		if (syms[relocs[i].sym].oidx < 0) fatal("internal error: relocation against dropped symbol");
	fwrite("TOF1", 1, 4, f);
	w32(f, secsize[1]);
	w32(f, secsize[2]);
	w32(f, secsize[3]);
	w32(f, no);
	w32(f, nrelocs);
	w32(f, strsz);
	if (secsize[1]) fwrite(secbuf[1], 1, secsize[1], f);
	if (secsize[2]) fwrite(secbuf[2], 1, secsize[2], f);
	strsz = 1;
	for (i = 0; i < no; i++) {
		Sym *s = &syms[order[i]];
		int sec = s->defined ? s->sec : 0;
		w32(f, strsz);
		w32(f, s->defined ? s->val : 0);
		fputc(sec, f);
		fputc(s->glob || !s->defined ? 1 : 0, f);
		fputc(0, f);
		fputc(0, f);
		strsz += strlen(s->name) + 1;
	}
	for (i = 0; i < nrelocs; i++) {
		w32(f, relocs[i].off);
		fputc(relocs[i].sec, f);
		fputc(relocs[i].type, f);
		fputc(0, f);
		fputc(0, f);
		w32(f, syms[relocs[i].sym].oidx);
		w32(f, relocs[i].add);
	}
	fputc(0, f);
	for (i = 0; i < no; i++) fwrite(syms[order[i]].name, 1, strlen(syms[order[i]].name) + 1, f);
	fclose(f);
}

static void write_flat(const char *out) {
	FILE *f = fopen(out, "wb");
	unsigned n = flatmax - origin;
	if (!f) {
		fprintf(stderr, "as: cannot create '%s'\n", out);
		exit(1);
	}
	if (n > secmax[1]) {
		secbuf[1] = xrealloc(secbuf[1], n);
		memset(secbuf[1] + secmax[1], 0, n - secmax[1]);
		secmax[1] = n;
	}
	if (n) fwrite(secbuf[1], 1, n, f);
	fclose(f);
	printf("%u bytes written to %s\n", n, out);
}

static void usage(void) {
	fprintf(stderr, "usage: as [-c] [-I dir] [-o out] in.asm [out]\n");
	exit(1);
}

int main(int argc, char *argv[]) {
	const char *in = NULL, *out = NULL;
	int i;
	for (i = 0; i < NHASH; i++) hasht[i] = -1;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0) objmode = 1;
		else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
		else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
			if (nincdirs < 16) incdirs[nincdirs++] = argv[++i];
			else i++;
		}
		else if (argv[i][0] == '-' && argv[i][1]) usage();
		else if (!in) in = argv[i];
		else if (!out) out = argv[i];
		else usage();
	}
	if (!in || !out) usage();
	sym_get(".text");
	sym_get(".data");
	sym_get(".bss");
	for (i = 0; i < 3; i++) {
		syms[i].defined = 1;
		syms[i].sec = i + 1;
		syms[i].val = 0;
	}
	cur_file = -1;
	read_file(in, 0);
	if (!objmode) {
		for (i = 0; i < 3; i++) syms[i].defined = 0;
	}
	run_pass(1);
	run_pass(2);
	cur_file = -1;
	if (objmode) write_obj(out);
	else write_flat(out);
	return 0;
}
