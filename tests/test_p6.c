#include <sys/wait.h>
#include "tutil.h"
#include "opts.h"

static uint8_t out[1 << 16];
static int outn;
static char err[8192];

static void wfile(const char *path, const char *s) {
	FILE *f = fopen(path, "w");
	fputs(s, f);
	fclose(f);
}

static int as_run(const char *opts, const char *src) {
	char cmd[512];
	FILE *f;
	int r;
	wfile("tests/p6.tmp.s", src);
	snprintf(cmd, sizeof(cmd), "./as %s tests/p6.tmp.s -o tests/p6.tmp.out > /dev/null 2> tests/p6.tmp.err", opts);
	remove("tests/p6.tmp.out");
	r = system(cmd);
	r = WIFEXITED(r) ? WEXITSTATUS(r) : -1;
	outn = 0;
	f = fopen("tests/p6.tmp.out", "rb");
	if (f) {
		outn = (int)fread(out, 1, sizeof(out), f);
		fclose(f);
	}
	err[0] = 0;
	f = fopen("tests/p6.tmp.err", "r");
	if (f) {
		size_t n = fread(err, 1, sizeof(err) - 1, f);
		err[n] = 0;
		fclose(f);
	}
	return r;
}

static uint32_t o32(int at) {
	return out[at] | out[at + 1] << 8 | out[at + 2] << 16 | (uint32_t)out[at + 3] << 24;
}

static void expect_bytes(const char *src, const uint8_t *want, int n) {
	int r = as_run("", src);
	CHECK(r == 0);
	CHECKV(outn == n, outn, n);
	if (outn == n) {
		int ok = memcmp(out, want, n) == 0;
		CHECK(ok);
		if (!ok) {
			fprintf(stderr, "  source: %s\n  got:", src);
			for (int i = 0; i < n; i++) fprintf(stderr, " %02X", out[i]);
			fprintf(stderr, "\n");
		}
	} else {
		fprintf(stderr, "  source: %s\n  err: %s\n", src, err);
	}
}

static void expect_dword(const char *expr, uint32_t v) {
	char src[1024];
	snprintf(src, sizeof(src), "start:\nNOP\nlab:\n.dword %s\n", expr);
	int r = as_run("", src);
	CHECK(r == 0 && outn == 5);
	CHECKV(o32(1) == v, o32(1), v);
	if (o32(1) != v) fprintf(stderr, "  expr: %s err: %s\n", expr, err);
}

static void expect_err(const char *opts, const char *src, int line, const char *msg) {
	char want[512];
	int r = as_run(opts, src);
	CHECKV(r == 1, r, line);
	snprintf(want, sizeof(want), "p6.tmp.s:%d: error: %s", line, msg);
	if (!strstr(err, want)) {
		fails++;
		fprintf(stderr, "error message mismatch: want '%s' got '%s'\n", want, err);
	}
	checks++;
}

static void test_expressions(void) {
	expect_dword("1 + 2 * 3", 7);
	expect_dword("(1 + 2) * 3", 9);
	expect_dword("100 / 7", 14);
	expect_dword("-100 / 7", (uint32_t)-14);
	expect_dword("100 % 7", 2);
	expect_dword("-7 % 3", (uint32_t)-1);
	expect_dword("0x10 | 0b101", 0x15);
	expect_dword("0xFF & 0x0F ^ 0x3", 0x0C);
	expect_dword("0xF0 ^ 0x0F & 0xFF", 0xFF);
	expect_dword("1 | 2 ^ 3 & 4", 3);
	expect_dword("1 << 4 + 1", 32);
	expect_dword("256 >> 2 >> 1", 32);
	expect_dword("~0", 0xFFFFFFFF);
	expect_dword("-1", 0xFFFFFFFF);
	expect_dword("- -5", 5);
	expect_dword("+5", 5);
	expect_dword("~0x0F & 0xFF", 0xF0);
	expect_dword("'A'", 65);
	expect_dword("'\\n' + '\\t' * 256", 10 + 9 * 256);
	expect_dword("'\\0'", 0);
	expect_dword("'\\\\'", '\\');
	expect_dword("'\\''", '\'');
	expect_dword("'\\x41'", 0x41);
	expect_dword("lab", 1);
	expect_dword("lab + 3", 4);
	expect_dword("lab - start", 1);
	expect_dword(".", 1);
	expect_dword(". - lab", 0);
	expect_dword("0B11", 3);
	expect_dword("0XaB", 0xAB);
	expect_dword("2 * (3 + (4 - 1)) % 5", 2);
	expect_dword("1 << 31", 0x80000000);
	expect_dword("10 - 2 - 3", 5);
	expect_dword("100 / 10 / 2", 5);
	char src[256];
	snprintf(src, sizeof(src), "NOP\n.dword fwd2 + 1\nfwd2:\n");
	CHECK(as_run("", src) == 0 && o32(1) == 6);
	snprintf(src, sizeof(src), ".equ A, 5\n.equ B, A * 2\n.dword B, A\nLOAD32 R1, B + A\n");
	static const uint8_t w1[] = {10, 0, 0, 0, 5, 0, 0, 0, 0x20, 1, 15, 0, 0, 0};
	expect_bytes(src, w1, sizeof(w1));
}

static void test_macros_labels(void) {
	static const uint8_t w1[] = {0x20, 1, 5, 0, 0, 0, 0x20, 2, 7, 0, 0, 0, 0x89, 0x12};
	expect_bytes(".macro ld2 a, b, x, y\nLOAD32 R\\a, \\x\nLOAD32 R\\b, \\y\n.endm\n.macro add2 p, q\nld2 \\p, \\q, 5, 7\nADD32 R\\p, R\\q\n.endm\nadd2 1, 2\n", w1, sizeof(w1));
	static const uint8_t w2[] = {0x00, 0x00, 0x00};
	expect_bytes(".macro n\nNOP\n.endm\nn\nn\nl: n\n", w2, sizeof(w2));
	static const uint8_t w3[] = {0x45, 0, 0, 0, 0, 0, 0x45, 0, 0, 0, 0, 0};
	expect_bytes(".macro skip\nBEQ .x\\@\n.x\\@:\n.endm\nskip\nskip\n", w3, sizeof(w3));
	static const uint8_t w4[] = {0x46, 0, 0xFA, 0xFF, 0xFF, 0xFF, 0x46, 0, 0xFA, 0xFF, 0xFF, 0xFF, 1, 0, 0, 0, 7, 0, 0, 0};
	expect_bytes("f1:\n.loop:\nBNE .loop\nf2:\n.loop:\nBNE .loop\n.dword f1.loop + 1, f2.loop + 1\n", w4, sizeof(w4));
	static const uint8_t w5[] = {0x43, 0, 0, 0, 0, 0, 0x43, 0, 0xF4, 0xFF, 0xFF, 0xFF};
	expect_bytes("a:\nJMPREL .end\n.end:\nJMPREL a\n", w5, sizeof(w5));
	static const uint8_t w6[] = {0x2A, 0x2A, 1};
	expect_bytes(".macro two c\n.byte \\c, \\c\n.endm\ntwo '*'\n.byte 1\n", w6, sizeof(w6));
}

static void test_include(void) {
	wfile("tests/p6inc.tmp.s", ".equ INCVAL, 0x1234\n.macro emit v\n.word \\v\n.endm\n");
	static const uint8_t w1[] = {0x34, 0x12, 0x34, 0x12};
	expect_bytes(".include \"p6inc.tmp.s\"\nemit INCVAL\n.word INCVAL\n", w1, sizeof(w1));
	wfile("tests/p6inc2.tmp.s", ".include \"p6inc.tmp.s\"\nNOP\n");
	static const uint8_t w2[] = {0, 0x34, 0x12};
	expect_bytes(".include \"p6inc2.tmp.s\"\nemit INCVAL\n", w2, sizeof(w2));
	remove("tests/p6inc2.tmp.s");
}

static void test_directives(void) {
	static const uint8_t w1[] = {1, 2, 3, 'h', 'i', 4, 0, 5, 0, 6, 0, 0, 0, 7, 0, 0, 0};
	expect_bytes(".byte 1, 2, 3, \"hi\"\n.word 4, 5\n.dword 6, 7\n", w1, sizeof(w1));
	static const uint8_t w2[] = {'a', '\n', 'b', '"', 'c', 'd', 0};
	expect_bytes(".ascii \"a\\nb\\\"\"\n.asciiz \"cd\"\n", w2, sizeof(w2));
	static const uint8_t w3[] = {1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
	expect_bytes(".byte 1\n.align 4\n.byte 2\n.align 16\n.byte 3\n.align 1\n", w3, sizeof(w3));
	static const uint8_t w4[] = {0, 0, 0, 0xAA, 0xAA, 9};
	expect_bytes(".space 3\n.space 2, 0xAA\n.byte 9\n", w4, sizeof(w4));
	static const uint8_t w5[] = {0, 0, 0, 0, 1};
	expect_bytes(".org 4\n.byte 1\n", w5, sizeof(w5));
	CHECK(as_run("", ".org 0xFFFF0000\nstart:\nJMP32 start\n.dword start\n") == 0);
	CHECK(outn == 10 && o32(2) == 0xFFFF0000 && o32(6) == 0xFFFF0000);
	static const uint8_t w6[] = {0x7A, 0, 0xF0, 0xFF, 0xFF, 0xFF, 0x72, 0x08, 4, 0, 0, 0, 0x73, 0x32, 1, 0, 0, 0, 0x77, 0x00, 0, 0, 0, 0, 0x7D, 0x12, 0x7E, 0x31, 0x72, 0x14, 0xFC, 0xFF, 0xFF, 0xFF};
	expect_bytes("ADDSP -16\nLDW R0, [SP + 4]\nSTB [R2 + 1], R3\nmsg:\nLEAPC R0, msg + 6\nMFCR R1, CR2\nMTCR CR3, R1\nLDW R1, [R4 - 4]\n", w6, sizeof(w6));
	CHECK(as_run("-c", ".text\n.globl f, g\nf:\nCALL32 g\nBEQ g\nBNE f\nLOAD R1, h\n.data\nd:\n.dword f + 8, d\n.word d\n.bss\nb:\n.space 100\n.align 8\n.text\n.byte 1\n") == 0);
	CHECK(memcmp(out, "TOF1", 4) == 0);
	uint32_t ts = o32(4), dsz = o32(8), bs = o32(12), ns = o32(16), nr = o32(20), strsz = o32(24);
	CHECKV(ts == 23, ts, 0);
	CHECKV(dsz == 10, dsz, 0);
	CHECKV(bs == 104, bs, 0);
	CHECK(outn == (int)(28 + ts + dsz + ns * 12 + nr * 16 + strsz));
	CHECK(o32(28 + 8) == 0);
	CHECK(o32(28 + 14) == (uint32_t)-18);
	CHECKV(nr == 6, nr, 0);
	int symoff = 28 + ts + dsz, reloff = symoff + ns * 12, stroff = reloff + nr * 16;
	int seen_g = 0, seen_f = 0, seen_h = 0, types[4] = {0, 0, 0, 0};
	for (uint32_t i = 0; i < ns; i++) {
		const char *nm = (const char *)out + stroff + o32(symoff + i * 12);
		uint8_t sec = out[symoff + i * 12 + 8], bind = out[symoff + i * 12 + 9];
		if (!strcmp(nm, "g")) seen_g = sec == 0 && bind == 1;
		if (!strcmp(nm, "f")) seen_f = sec == 1 && bind == 1 && o32(symoff + i * 12 + 4) == 0;
		if (!strcmp(nm, "h")) seen_h = sec == 0 && bind == 1;
		if (!strcmp(nm, "b")) CHECK(sec == 3 && bind == 0);
		if (!strcmp(nm, "d")) CHECK(sec == 2 && bind == 0);
	}
	CHECK(seen_g && seen_f && seen_h);
	for (uint32_t i = 0; i < nr; i++) {
		uint8_t t = out[reloff + i * 16 + 5];
		if (t < 4) types[t]++;
	}
	CHECK(types[1] == 3 && types[2] == 1 && types[3] == 2);
}

static void test_errors(void) {
	char big[6000];
	expect_err("", "NOP\nFOO R1\n", 2, "unknown instruction 'FOO'");
	expect_err("", ".bogus 1\n", 1, "unknown directive .bogus");
	expect_err("", "LOAD32 R1, nothere\n", 1, "undefined symbol 'nothere'");
	expect_err("", "x:\nx:\n", 2, "duplicate symbol 'x'");
	expect_err("", ".dword (1 + 2\n", 1, "missing ')'");
	expect_err("", ".dword 'ab'\n", 1, "bad character literal");
	expect_err("", ".dword 0xZ\n", 1, "bad hex number");
	expect_err("", ".dword 12a\n", 1, "bad digit 'a' in number");
	expect_err("", ".dword 1 +\n", 1, "expression expected");
	expect_err("", ".dword 1 + #\n", 1, "unexpected '#' in expression");
	expect_err("", ".dword 1 / 0\n", 1, "division by zero in expression");
	expect_err("", ".dword 1 2\n", 1, "junk '2' after expression");
	expect_err("", ".dword '\\q'\n", 1, "unknown escape '\\q'");
	expect_err("", ".dword '\n", 1, "unterminated literal");
	expect_err("", "x:\n.space x\n.space -1\n", 3, ".space too large");
	expect_err("", ".space\n", 1, ".space needs a size");
	expect_err("", ".align 3\n", 1, ".align needs a power of two");
	expect_err("", ".equ X\n", 1, ".equ needs a name and a value");
	expect_err("", ".equ X, 1\n.equ X, 2\n", 2, "duplicate symbol 'X'");
	expect_err("", ".asciiz abc\n", 1, "string expected");
	expect_err("", ".asciiz \"abc\n", 1, "unterminated string");
	expect_err("", ".asciiz \"abc\" x\n", 1, "junk after string");
	expect_err("", ".text\n", 1, "section directives need -c");
	expect_err("-c", ".text x\n", 1, "junk after .text");
	expect_err("-c", ".org 4\n", 1, ".org is not allowed in an object file");
	expect_err("", ".org 0xFFFF0000\nNOP\n.org 0x10\n", 3, ".org below the start of the output");
	expect_err("-c", ".bss\n.byte 1\n", 2, "data not allowed in .bss");
	expect_err("-c", ".bss\n.space 4, 1\n", 2, "fill value not allowed in .bss");
	expect_err("", ".org 0x4000000\n.byte 1\n", 2, "output too large");
	expect_err("-c", ".globl 1x\n", 1, "symbol name expected");
	expect_err("-c", ".byte ext\n", 1, "relocatable value does not fit in 8 bits");
	expect_err("-c", "BEQ 0x100\n", 1, "branch to an absolute address is not allowed in an object file");
	expect_err("-c", "a:\n.dword a + a\n", 2, "cannot add two relocatable values");
	expect_err("-c", "a:\n.data\nb:\n.dword a - b\n", 4, "invalid difference of relocatable values");
	expect_err("-c", "a:\n.dword a * 2\n", 2, "relocatable value not allowed with this operator");
	expect_err("-c", "a:\n.space a\n", 2, "absolute expression required");
	expect_err("-c", "a:\nLDW R1, [R2 - a]\n", 2, "cannot negate a relocatable offset");
	expect_err("", "LDW R1, [R2 + 4\n", 1, "missing ']'");
	expect_err("", "LDW R1, [Q2 + 4]\n", 1, "base register expected in memory operand");
	expect_err("", "LDW R1, [R2 * 4]\n", 1, "bad memory operand");
	expect_err("", "NOP R1\n", 1, "NOP expects 0 operands");
	expect_err("", "NEG\n", 1, "NEG expects 1 operand");
	expect_err("", "ADC R1\n", 1, "ADC expects 2 operands");
	expect_err("", "ADC R1, R2, R3\n", 1, "ADC: too many operands");
	expect_err("", "PUSH 5\n", 1, "PUSH: register R0 to R7 expected");
	expect_err("", "PUSH R9\n", 1, "PUSH: register R0 to R7 expected");
	expect_err("", "INT R1\n", 1, "INT: expression expected");
	expect_err("", "LDW R1, 5\n", 1, "LDW: memory operand expected");
	expect_err("", "STW R1, R2\n", 1, "STW: memory operand expected");
	expect_err("", "LOAD R1, [SP + 4]\n", 1, "LOAD: SP cannot be used as base register here");
	expect_err("", "SHL32 R1, R2\n", 1, "SHL32: register operand not allowed");
	expect_err("", "MFCR R1, R2\n", 1, "MFCR: control register expected");
	expect_err("", "MTCR R1, R2\n", 1, "MTCR: control register expected");
	expect_err("", "MFCR R1, CR16\n", 1, "MFCR: control register expected");
	expect_err("", "LOAD R1,,\n", 1, "LOAD: too many operands");
	expect_err("", "+\n", 1, "syntax error");
	expect_err("", "NOP+\n", 1, "syntax error near '+'");
	expect_err("", ".macro\n", 1, "macro name expected");
	expect_err("", ".macro m 1a\n.endm\n", 1, "bad macro parameter '1a'");
	expect_err("", ".macro m a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q\n.endm\n", 1, "too many macro parameters");
	expect_err("", ".macro m\n.macro n\n", 2, "nested macro definition");
	expect_err("", ".endm\n", 1, ".endm without .macro");
	expect_err("", ".macro m\nNOP\n", 2, "missing .endm");
	expect_err("", ".macro m a\nNOP\n.endm\nm 1, 2\n", 4, "too many arguments for macro m");
	expect_err("", ".macro m\nm\n.endm\nm\n", 4, "macro expansion too deep");
	expect_err("", ".include p6inc.tmp.s\n", 1, ".include needs a quoted file name");
	expect_err("", ".include \"p6inc.tmp.s\n", 1, "unterminated file name");
	expect_err("", ".include \"nonexistent.s\"\n", 1, "cannot open include file 'nonexistent.s'");
	wfile("tests/p6self.tmp.s", ".include \"p6self.tmp.s\"\n");
	as_run("", ".include \"p6self.tmp.s\"\n");
	CHECK(strstr(err, "p6self.tmp.s:1: error: include nested too deeply") != NULL);
	remove("tests/p6self.tmp.s");
	{
		char *s = malloc(300 * 32);
		s[0] = 0;
		for (int i = 0; i < 300; i++) strcat(s, ".include \"p6inc.tmp.s\"\n");
		as_run("", s);
		CHECK(strstr(err, "error: too many files") != NULL);
		free(s);
	}
	memset(big, 'a', 5000);
	big[5000] = 0;
	{
		char *s = malloc(6000);
		snprintf(s, 6000, "%s:\n", big);
		expect_err("", s, 1, "line too long");
		big[240] = 0;
		snprintf(s, 6000, "%s:\n", big);
		expect_err("", s, 1, "label name too long");
		snprintf(s, 6000, ".dword %s%s\n", big, "bbbbbbbbbbbbbbbbbbbb");
		expect_err("", s, 1, "symbol name too long");
		big[240] = 'a';
		big[3000] = 0;
		snprintf(s, 6000, ".macro m a\n.byte \\a\\a\n.endm\nm %s\n", big);
		expect_err("", s, 4, "macro line too long");
		free(s);
	}
	expect_err("", ".byte 1,2,3,4,5,6,7,8\nLOAD R1, R2, R3, R4\n", 2, "too many operands");
	CHECK(system("./as tests/none.s tests/none.bin > /dev/null 2>&1") != 0);
	CHECK(system("./as > /dev/null 2>&1") != 0);
}

static int sh(const char *cmd) {
	int r = system(cmd);
	return WIFEXITED(r) ? WEXITSTATUS(r) : -1;
}

static void test_linker_errors(void) {
	wfile("tests/l1.tmp.s", ".text\n.globl _start, dup\n_start:\ndup:\nCALL32 missing\n");
	wfile("tests/l2.tmp.s", ".text\n.globl dup\ndup:\nNOP\n");
	wfile("tests/l3.tmp.s", ".text\n.globl missing\nmissing:\nLOAD R1, far\n.data\n.space 0x10000\nfar:\n");
	CHECK(sh("./as -c tests/l1.tmp.s -o tests/l1.tmp.o > /dev/null") == 0);
	CHECK(sh("./as -c tests/l2.tmp.s -o tests/l2.tmp.o > /dev/null") == 0);
	CHECK(sh("./as -c tests/l3.tmp.s -o tests/l3.tmp.o > /dev/null") == 0);
	CHECK(sh("./tlink -o tests/l.tmp.tex tests/l1.tmp.o 2> tests/l.tmp.err") == 1);
	CHECK(sh("grep -q 'undefined symbol missing' tests/l.tmp.err") == 0);
	CHECK(sh("./tlink -o tests/l.tmp.tex tests/l1.tmp.o tests/l2.tmp.o 2> tests/l.tmp.err") == 1);
	CHECK(sh("grep -q 'duplicate symbol dup' tests/l.tmp.err") == 0);
	CHECK(sh("./tlink -o tests/l.tmp.tex tests/l1.tmp.o tests/l3.tmp.o 2> tests/l.tmp.err") == 1);
	CHECK(sh("grep -q '16 bit relocation overflow' tests/l.tmp.err") == 0);
	CHECK(sh("./tlink -e nosuch -o tests/l.tmp.tex tests/l2.tmp.o 2> /dev/null") == 1);
	CHECK(sh("./tlink -o tests/l.tmp.tex tests/l.tmp.err 2> /dev/null") == 1);
	CHECK(sh("./tlink -o tests/l.tmp.tex 2> /dev/null") == 1);
	CHECK(sh("./tlink -e dup -b -T 0x100 -o tests/l.tmp.tex tests/l2.tmp.o") == 0);
	FILE *f = fopen("tests/l.tmp.tex", "rb");
	uint8_t b[8];
	CHECK(fread(b, 1, 8, f) == 1 && b[0] == 0);
	fclose(f);
	wfile("tests/l4.tmp.s", ".text\n.globl _start\n_start:\nLOAD32 R1, _etext\nLOAD32 R2, _edata\nLOAD32 R3, _end\nLOAD32 R4, dat\nLOAD32 R5, bs\nHALT\n.data\ndat:\n.dword 7\n.bss\nbs:\n.space 12\n");
	CHECK(sh("./as -c tests/l4.tmp.s -o tests/l4.tmp.o > /dev/null") == 0);
	CHECK(sh("./tlink -T 0x2000 -o tests/l.tmp.tex tests/l2.tmp.o tests/l4.tmp.o") == 0);
	f = fopen("tests/l.tmp.tex", "rb");
	uint8_t h[32];
	CHECK(fread(h, 1, 32, f) == 32);
	fclose(f);
	CHECK(memcmp(h, "TEX1", 4) == 0);
	uint32_t entry = h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24;
	uint32_t ta = h[8] | h[9] << 8 | h[10] << 16 | (uint32_t)h[11] << 24;
	uint32_t ts = h[12] | h[13] << 8;
	uint32_t da = h[16] | h[17] << 8 | h[18] << 16 | (uint32_t)h[19] << 24;
	uint32_t bs = h[24] | h[25] << 8;
	CHECK(entry == 0x2004 && ta == 0x2000 && ts == 4 + 31 && da == 0x3000 && bs == 12);
	cpu_init();
	CHECK(load_image("tests/l.tmp.tex") == 0);
	cpu_run();
	CHECK(cpu.registers[1] == 0x2000 + 35);
	CHECK(cpu.registers[2] == 0x3004);
	CHECK(cpu.registers[3] == 0x3004 + 12);
	CHECK(cpu.registers[4] == 0x3000);
	CHECK(cpu.registers[5] == 0x3004);
	CHECK(rd32(0x3000) == 7);
	remove("tests/l1.tmp.s");
	remove("tests/l2.tmp.s");
	remove("tests/l3.tmp.s");
	remove("tests/l4.tmp.s");
	remove("tests/l1.tmp.o");
	remove("tests/l2.tmp.o");
	remove("tests/l3.tmp.o");
	remove("tests/l4.tmp.o");
	remove("tests/l.tmp.tex");
	remove("tests/l.tmp.err");
}

int main(void) {
	test_expressions();
	test_macros_labels();
	test_include();
	test_directives();
	test_errors();
	test_linker_errors();
	remove("tests/p6inc.tmp.s");
	remove("tests/p6.tmp.s");
	remove("tests/p6.tmp.out");
	remove("tests/p6.tmp.err");
	return report("test_p6");
}
