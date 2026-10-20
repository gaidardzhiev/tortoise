#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "dev.h"
#include "dis.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKV(c, a, b) do { checks++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: check failed: %s (a=0x%08X b=0x%08X)\n", __FILE__, __LINE__, #c, (unsigned)(a), (unsigned)(b)); } } while (0)

static const uint32_t edges[] = {0, 1, 2, 0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x12345678, 0xFFFF0000, 31, 32, 33};
#define NEDGE (sizeof(edges) / sizeof(edges[0]))

static uint8_t prog[4096];
static int plen;

static void b(int v) { prog[plen++] = (uint8_t)v; }
static void d32(uint32_t v) { b(v); b(v >> 8); b(v >> 16); b(v >> 24); }
static void opR(int op, int a, int bb) { b(op); b(a << 4 | bb); }
static void opRI(int op, int a, uint32_t imm) { b(op); b(a << 4); d32(imm); }
static void opI(int op, uint32_t imm) { b(op); b(0); d32(imm); }
static void opM(int op, int a, int bb, uint32_t d) { b(op); b(a << 4 | bb); d32(d); }
static void load32(int r, uint32_t v) { b(OP_LOAD32); b(r); d32(v); }

static void start(void) {
	plen = 0;
}

static void go(uint32_t *regs, uint8_t flags) {
	cpu_init();
	load_program(prog, plen);
	if (regs) memcpy(cpu.registers, regs, sizeof(cpu.registers));
	cpu.flags = flags;
	cpu_run();
}

static uint8_t zs(uint32_t r) {
	return (r == 0 ? FLAG_Z : 0) | ((r >> 31) ? FLAG_S : 0);
}

static uint8_t ref_add(uint32_t a, uint32_t bb, uint32_t c, uint32_t *r) {
	uint64_t u = (uint64_t)a + bb + c;
	int64_t s = (int64_t)(int32_t)a + (int32_t)bb + (int64_t)c;
	*r = (uint32_t)u;
	return zs(*r) | (u > 0xFFFFFFFFull ? FLAG_C : 0) | ((s > 2147483647LL || s < -2147483648LL) ? FLAG_V : 0);
}

static uint8_t ref_sub(uint32_t a, uint32_t bb, uint32_t c, uint32_t *r) {
	int64_t s = (int64_t)(int32_t)a - (int32_t)bb - (int64_t)c;
	*r = a - bb - c;
	return zs(*r) | ((uint64_t)bb + c > a ? FLAG_C : 0) | ((s > 2147483647LL || s < -2147483648LL) ? FLAG_V : 0);
}

static void rr(int op, uint32_t a, uint32_t bb, uint8_t fin, uint32_t *r, uint8_t *f) {
	uint32_t regs[8] = {0};
	start();
	opR(op, 1, 2);
	b(OP_HALT);
	regs[1] = a;
	regs[2] = bb;
	go(regs, fin);
	*r = cpu.registers[1];
	*f = cpu.flags & 0x0F;
}

static void ri(int op, uint32_t a, uint32_t imm, uint8_t fin, uint32_t *r, uint8_t *f) {
	uint32_t regs[8] = {0};
	start();
	opRI(op, 3, imm);
	b(OP_HALT);
	regs[3] = a;
	go(regs, fin);
	*r = cpu.registers[3];
	*f = cpu.flags & 0x0F;
}

static void test_addsub(void) {
	for (unsigned i = 0; i < NEDGE; i++)
		for (unsigned j = 0; j < NEDGE; j++)
			for (uint32_t c = 0; c < 2; c++) {
				uint32_t a = edges[i], bb = edges[j], r, er;
				uint8_t f, ef, fin = c ? FLAG_C : 0;
				ef = ref_add(a, bb, c, &er);
				rr(OP_ADC, a, bb, fin, &r, &f);
				CHECKV(r == er && f == ef, a, bb);
				ri(OP_ADCI, a, bb, fin, &r, &f);
				CHECKV(r == er && f == ef, a, bb);
				ef = ref_sub(a, bb, c, &er);
				rr(OP_SBB, a, bb, fin, &r, &f);
				CHECKV(r == er && f == ef, a, bb);
				ri(OP_SBBI, a, bb, fin, &r, &f);
				CHECKV(r == er && f == ef, a, bb);
				ef = ref_add(a, bb, 0, &er);
				rr(OP_ADD32R, a, bb, fin, &r, &f);
				CHECKV(r == er && f == ef, a, bb);
				ef = ref_sub(a, bb, 0, &er);
				rr(OP_SUB32R, a, bb, fin, &r, &f);
				CHECKV(r == er && f == ef, a, bb);
				rr(OP_CMP32R, a, bb, fin, &r, &f);
				CHECKV(r == a && f == ef, a, bb);
			}
	uint32_t r;
	uint8_t f;
	rr(OP_ADC, 0xFFFFFFFF, 0, FLAG_C, &r, &f);
	CHECK(r == 0 && f == (FLAG_Z | FLAG_C));
	rr(OP_ADC, 0x7FFFFFFF, 0, FLAG_C, &r, &f);
	CHECK(r == 0x80000000 && f == (FLAG_S | FLAG_V));
	rr(OP_SBB, 0, 0, FLAG_C, &r, &f);
	CHECK(r == 0xFFFFFFFF && f == (FLAG_S | FLAG_C));
	rr(OP_SBB, 0x80000000, 0, FLAG_C, &r, &f);
	CHECK(r == 0x7FFFFFFF && f == FLAG_V);
}

static void test_neg(void) {
	for (unsigned i = 0; i < NEDGE; i++) {
		uint32_t a = edges[i], r, er;
		uint8_t ef = ref_sub(0, a, 0, &er), f;
		uint32_t regs[8] = {0};
		start();
		opR(OP_NEG, 4, 0);
		b(OP_HALT);
		regs[4] = a;
		go(regs, FLAG_C | FLAG_V);
		r = cpu.registers[4];
		f = cpu.flags & 0x0F;
		CHECKV(r == er && f == ef, a, 0);
	}
	uint32_t regs[8] = {0};
	start();
	opR(OP_NEG, 4, 0);
	b(OP_HALT);
	regs[4] = 0x80000000;
	go(regs, 0);
	CHECK(cpu.registers[4] == 0x80000000 && (cpu.flags & 0xF) == (FLAG_S | FLAG_V | FLAG_C));
	regs[4] = 0;
	go(regs, FLAG_C);
	CHECK(cpu.registers[4] == 0 && (cpu.flags & 0xF) == FLAG_Z);
}

static uint32_t ref_shift(int kind, uint32_t a, uint32_t n, uint8_t fin, uint8_t *f) {
	uint32_t r = a;
	int c = -1;
	n &= 31;
	for (uint32_t k = 0; k < n; k++) {
		switch (kind) {
		case 0: c = r >> 31; r <<= 1; break;
		case 1: c = r & 1; r >>= 1; break;
		case 2: c = r & 1; r = (r >> 1) | (r & 0x80000000u); break;
		case 3: c = r >> 31; r = (r << 1) | (uint32_t)c; break;
		default: c = r & 1; r = (r >> 1) | ((uint32_t)c << 31); break;
		}
	}
	if (c < 0) *f = zs(r) | (fin & (FLAG_C | FLAG_V));
	else *f = zs(r) | (c ? FLAG_C : 0);
	return r;
}

static void test_shifts(void) {
	static const int vops[5] = {OP_SHLV, OP_SHRV, OP_SARV, OP_ROLV, OP_RORV};
	static const int iops[5] = {0, 0, OP_SAR, OP_ROL, OP_ROR};
	static const uint32_t counts[] = {0, 1, 2, 4, 15, 16, 31, 32, 33, 63, 0xFFFFFFFF};
	for (int k = 0; k < 5; k++)
		for (unsigned i = 0; i < NEDGE; i++)
			for (unsigned j = 0; j < sizeof(counts) / sizeof(counts[0]); j++)
				for (int fl = 0; fl < 2; fl++) {
					uint8_t fin = fl ? (FLAG_C | FLAG_V | FLAG_Z | FLAG_S) : 0, f, ef;
					uint32_t a = edges[i], n = counts[j], r;
					uint32_t er = ref_shift(k, a, n, fin, &ef);
					rr(vops[k], a, n, fin, &r, &f);
					CHECKV(r == er && f == ef, a, n);
					if (iops[k]) {
						ri(iops[k], a, n, fin, &r, &f);
						CHECKV(r == er && f == ef, a, n);
					}
				}
	uint32_t r;
	uint8_t f;
	rr(OP_SHLV, 0x80000001, 1, 0, &r, &f);
	CHECK(r == 2 && f == FLAG_C);
	rr(OP_SARV, 0x80000000, 31, 0, &r, &f);
	CHECK(r == 0xFFFFFFFF && f == FLAG_S);
	rr(OP_SHRV, 5, 0, FLAG_C | FLAG_V, &r, &f);
	CHECK(r == 5 && f == (FLAG_C | FLAG_V));
	rr(OP_RORV, 1, 1, 0, &r, &f);
	CHECK(r == 0x80000000 && f == (FLAG_S | FLAG_C));
	rr(OP_ROLV, 0x80000000, 33, 0, &r, &f);
	CHECK(r == 1 && f == FLAG_C);
}

static void test_mul(void) {
	for (unsigned i = 0; i < NEDGE; i++)
		for (unsigned j = 0; j < NEDGE; j++) {
			uint32_t a = edges[i], bb = edges[j], r;
			uint8_t f;
			int64_t sp = (int64_t)(int32_t)a * (int64_t)(int32_t)bb;
			uint64_t up = (uint64_t)a * bb;
			uint32_t lo = (uint32_t)up;
			int ov = sp < -2147483648LL || sp > 2147483647LL;
			uint8_t ef = zs(lo) | (ov ? FLAG_C | FLAG_V : 0);
			rr(OP_IMUL, a, bb, FLAG_Z, &r, &f);
			CHECKV(r == lo && f == ef, a, bb);
			ri(OP_IMULI, a, bb, FLAG_Z, &r, &f);
			CHECKV(r == lo && f == ef, a, bb);
			rr(OP_MULHU, a, bb, FLAG_C | FLAG_V, &r, &f);
			CHECKV(r == (uint32_t)(up >> 32) && f == zs((uint32_t)(up >> 32)), a, bb);
			rr(OP_MULHS, a, bb, FLAG_C | FLAG_V, &r, &f);
			CHECKV(r == (uint32_t)((uint64_t)sp >> 32) && f == zs((uint32_t)((uint64_t)sp >> 32)), a, bb);
			rr(OP_MUL32R, a, bb, 0, &r, &f);
			CHECKV(r == lo && f == (zs(lo) | ((up >> 32) ? FLAG_C | FLAG_V : 0)), a, bb);
		}
	uint32_t r;
	uint8_t f;
	rr(OP_IMUL, 0x10000, 0x10000, 0, &r, &f);
	CHECK(r == 0 && f == (FLAG_Z | FLAG_C | FLAG_V));
	rr(OP_IMUL, 0xFFFFFFFF, 0xFFFFFFFF, 0, &r, &f);
	CHECK(r == 1 && f == 0);
	rr(OP_IMUL, 0x80000000, 0xFFFFFFFF, 0, &r, &f);
	CHECK(r == 0x80000000 && f == (FLAG_S | FLAG_C | FLAG_V));
	rr(OP_MULHS, 0xFFFFFFFF, 2, 0, &r, &f);
	CHECK(r == 0xFFFFFFFF && f == FLAG_S);
	rr(OP_MULHU, 0xFFFFFFFF, 2, 0, &r, &f);
	CHECK(r == 1 && f == 0);
}

static void div_handler_prog(int op, int imm, uint32_t immv) {
	start();
	load32(0, 0x40);
	b(OP_STORE);
	b(0);
	b(IVT_BASE & 0xFF);
	b(IVT_BASE >> 8);
	if (imm) opRI(op, 1, immv);
	else opR(op, 1, 2);
	b(OP_HALT);
	while (plen < 0x40) b(OP_NOP);
	load32(7, 0xD1D1);
	b(OP_HALT);
}

static void test_div(void) {
	static const int rops[4] = {OP_IDIV, OP_UMOD, OP_IMOD, OP_DIV32R};
	static const int iops[4] = {OP_IDIVI, OP_UMODI, OP_IMODI, 0};
	for (unsigned i = 0; i < NEDGE; i++)
		for (unsigned j = 0; j < NEDGE; j++) {
			uint32_t a = edges[i], bb = edges[j], r, er[4];
			uint8_t f;
			if (bb == 0) continue;
			if (a == 0x80000000 && bb == 0xFFFFFFFF) continue;
			er[0] = (uint32_t)((int32_t)a / (int32_t)bb);
			er[1] = a % bb;
			er[2] = (uint32_t)((int32_t)a % (int32_t)bb);
			er[3] = a / bb;
			for (int k = 0; k < 4; k++) {
				rr(rops[k], a, bb, FLAG_C | FLAG_V, &r, &f);
				CHECKV(r == er[k] && f == zs(er[k]), a, bb);
				if (iops[k]) {
					ri(iops[k], a, bb, FLAG_C | FLAG_V, &r, &f);
					CHECKV(r == er[k] && f == zs(er[k]), a, bb);
				}
			}
		}
	uint32_t r;
	uint8_t f;
	rr(OP_IDIV, (uint32_t)-7, 2, 0, &r, &f);
	CHECK(r == (uint32_t)-3);
	rr(OP_IMOD, (uint32_t)-7, 2, 0, &r, &f);
	CHECK(r == (uint32_t)-1);
	rr(OP_IMOD, 7, (uint32_t)-2, 0, &r, &f);
	CHECK(r == 1);
	rr(OP_UMOD, 0xFFFFFFFF, 10, 0, &r, &f);
	CHECK(r == 5);
	rr(OP_UMOD, 0x80000000, 0xFFFFFFFF, 0, &r, &f);
	CHECK(r == 0x80000000);
	for (int k = 0; k < 4; k++) {
		for (int imm = 0; imm < 2; imm++) {
			uint32_t pairs[3][2] = {{5, 0}, {0, 0}, {0x80000000, 0xFFFFFFFF}};
			int np = (k == 1 || k == 3) ? 2 : 3;
			if (imm && !iops[k]) continue;
			for (int p = 0; p < np; p++) {
				uint32_t regs[8] = {0};
				div_handler_prog(imm ? iops[k] : rops[k], imm, pairs[p][1]);
				regs[1] = pairs[p][0];
				regs[2] = pairs[p][1];
				go(regs, 0);
				CHECK(cpu.exit_code == 0);
				CHECK(cpu.registers[7] == 0xD1D1);
				CHECK(cpu.registers[1] == pairs[p][0]);
				CHECK(cpu.SP == MEMORY_SIZE - 4);
			}
		}
	}
	uint32_t regs[8] = {0};
	regs[1] = 0x80000001;
	regs[2] = 0xFFFFFFFF;
	rr(OP_IDIV, 0x80000001, 0xFFFFFFFF, 0, &r, &f);
	CHECK(r == 0x7FFFFFFF);
	(void)regs;
}

static int bcond(int op, uint8_t fl) {
	int z = !!(fl & FLAG_Z), c = !!(fl & FLAG_C), s = !!(fl & FLAG_S), v = !!(fl & FLAG_V);
	switch (op) {
	case OP_BEQ: return z;
	case OP_BNE: return !z;
	case OP_BLT: return s ^ v;
	case OP_BGE: return !(s ^ v);
	case OP_BLE: return z | (s ^ v);
	case OP_BGT: return !z & !(s ^ v);
	case OP_BLO: return c;
	case OP_BHS: return !c;
	case OP_BLS: return c | z;
	case OP_BHI: return !c & !z;
	case OP_BMI: return s;
	case OP_BPL: return !s;
	case OP_BVS: return v;
	default: return !v;
	}
}

static void test_branches(void) {
	for (int op = OP_BEQ; op <= OP_BVC; op++)
		for (int fl = 0; fl < 16; fl++)
			for (int dir = 0; dir < 2; dir++) {
				start();
				if (dir) {
					opI(OP_JMPREL, 14);
					load32(6, 0xAAAA);
					b(OP_HALT);
					load32(6, 0xBBBB);
					b(OP_HALT);
					opI(op, (uint32_t)-13);
					load32(6, 0xCCCC);
					b(OP_HALT);
				} else {
					opI(op, 7);
					load32(6, 0xCCCC);
					b(OP_HALT);
					load32(6, 0xBBBB);
					b(OP_HALT);
				}
				go(NULL, (uint8_t)fl);
				uint32_t want = bcond(op, (uint8_t)fl) ? 0xBBBB : 0xCCCC;
				CHECKV(cpu.registers[6] == want, op, fl);
				CHECK((cpu.flags & 0xF) == fl);
			}
	for (unsigned i = 0; i < NEDGE; i++)
		for (unsigned j = 0; j < NEDGE; j++) {
			uint32_t a = edges[i], bb = edges[j];
			int32_t sa = (int32_t)a, sb = (int32_t)bb;
			int want[16];
			want[OP_BEQ - OP_BEQ] = a == bb;
			want[OP_BNE - OP_BEQ] = a != bb;
			want[OP_BLT - OP_BEQ] = sa < sb;
			want[OP_BGE - OP_BEQ] = sa >= sb;
			want[OP_BLE - OP_BEQ] = sa <= sb;
			want[OP_BGT - OP_BEQ] = sa > sb;
			want[OP_BLO - OP_BEQ] = a < bb;
			want[OP_BHS - OP_BEQ] = a >= bb;
			want[OP_BLS - OP_BEQ] = a <= bb;
			want[OP_BHI - OP_BEQ] = a > bb;
			for (int op = OP_BEQ; op <= OP_BHI; op++) {
				uint32_t regs[8] = {0};
				start();
				opR(OP_CMP32R, 1, 2);
				opI(op, 7);
				load32(6, 0xCCCC);
				b(OP_HALT);
				load32(6, 0xBBBB);
				b(OP_HALT);
				regs[1] = a;
				regs[2] = bb;
				go(regs, 0);
				CHECKV(cpu.registers[6] == (want[op - OP_BEQ] ? 0xBBBBu : 0xCCCCu), a, bb);
			}
		}
}

static void test_jumps_calls(void) {
	uint32_t regs[8] = {0};
	cpu_init();
	start();
	opI(OP_JMP32, 0x30000);
	load_program(prog, plen);
	cpu.memory[0x30000] = OP_LOAD32;
	cpu.memory[0x30001] = 5;
	cpu.memory[0x30002] = 0x55;
	cpu.memory[0x30003] = 0;
	cpu.memory[0x30004] = 0;
	cpu.memory[0x30005] = 0;
	cpu.memory[0x30006] = OP_HALT;
	cpu_run();
	CHECK(cpu.registers[5] == 0x55 && cpu.exit_code == 0);
	start();
	opR(OP_JMPR, 0, 3);
	b(OP_HALT);
	load32(4, 9);
	b(OP_HALT);
	regs[3] = 3;
	go(regs, 0);
	CHECK(cpu.registers[4] == 9 && cpu.PC == 10);
	start();
	opR(OP_CALLR, 0, 3);
	b(OP_HALT);
	b(OP_POP32);
	b(5 << 4);
	b(OP_PUSH32);
	b(5);
	b(OP_RET32);
	regs[3] = 3;
	go(regs, 0);
	CHECK(cpu.registers[5] == 2);
	CHECK(cpu.SP == MEMORY_SIZE);
	CHECK(cpu.PC == 3);
	start();
	opI(OP_CALLREL, 1);
	b(OP_HALT);
	load32(1, 0x77);
	b(OP_RET32);
	go(NULL, 0);
	CHECK(cpu.registers[1] == 0x77 && cpu.PC == 7 && cpu.SP == MEMORY_SIZE);
	CHECK(cpu.memory[MEMORY_SIZE - 4] == 6 && cpu.memory[MEMORY_SIZE - 3] == 0);
	start();
	opI(OP_CALL32, 0x20000);
	b(OP_HALT);
	cpu_init();
	load_program(prog, plen);
	cpu.memory[0x20000] = OP_LOAD32;
	cpu.memory[0x20001] = 2;
	cpu.memory[0x20002] = 0x99;
	cpu.memory[0x20006] = OP_RET32;
	cpu_run();
	CHECK(cpu.registers[2] == 0x99 && cpu.PC == 7 && cpu.SP == MEMORY_SIZE);
	start();
	load32(1, 0xCAFEBABE);
	opR(OP_PUSH32, 0, 1);
	opR(OP_POP32, 2, 0);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.registers[2] == 0xCAFEBABE && cpu.SP == MEMORY_SIZE);
	CHECK(cpu.memory[MEMORY_SIZE - 4] == 0xBE && cpu.memory[MEMORY_SIZE - 1] == 0xCA);
}

static void test_mem(void) {
	uint32_t regs[8] = {0};
	start();
	b(OP_HALT);
	cpu_init();
	load_program(prog, plen);
	uint8_t data[8] = {0x81, 0x7F, 0x34, 0x92, 0x00, 0x80, 0xFF, 0x01};
	memcpy(cpu.memory + 0x5000, data, 8);
	start();
	load32(1, 0x5000);
	opM(OP_LDB, 2, 1, 0);
	opM(OP_LDBS, 3, 1, 0);
	opM(OP_LDBS, 4, 1, 1);
	opM(OP_LDH, 5, 1, 2);
	opM(OP_LDHS, 6, 1, 2);
	opM(OP_LDW, 7, 1, 4);
	b(OP_HALT);
	load_program(prog, plen);
	cpu_run();
	CHECK(cpu.registers[2] == 0x81);
	CHECK(cpu.registers[3] == 0xFFFFFF81);
	CHECK(cpu.registers[4] == 0x7F);
	CHECK(cpu.registers[5] == 0x9234);
	CHECK(cpu.registers[6] == 0xFFFF9234);
	CHECK(cpu.registers[7] == 0x01FF8000);
	start();
	load32(1, 0x6010);
	load32(2, 0xA1B2C3D4);
	opM(OP_STB, 2, 1, (uint32_t)-16);
	opM(OP_STH, 2, 1, (uint32_t)-14);
	opM(OP_STW, 2, 1, (uint32_t)-12);
	opM(OP_LDW, 3, 1, (uint32_t)-16);
	opM(OP_LDW, 4, 1, (uint32_t)-12);
	opM(OP_LDHS, 5, 1, (uint32_t)-15);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.memory[0x6000] == 0xD4);
	CHECK(cpu.memory[0x6001] == 0x00);
	CHECK(cpu.memory[0x6002] == 0xD4 && cpu.memory[0x6003] == 0xC3);
	CHECK(cpu.registers[3] == 0xC3D400D4);
	CHECK(cpu.registers[4] == 0xA1B2C3D4);
	CHECK(cpu.registers[5] == 0xFFFFD400);
	start();
	opRI(OP_ADDSP, 0, 0);
	prog[0] = OP_ADDSP;
	prog[1] = 0;
	plen = 2;
	d32((uint32_t)-16);
	load32(1, 0x11223344);
	opM(OP_STW, 1, 8, 4);
	opM(OP_LDW, 2, 8, 4);
	opM(OP_LEA, 3, 8, 12);
	opM(OP_LEA, 4, 1, (uint32_t)-4);
	opR(OP_MOVSP, 5, 0);
	load32(6, 0x8000);
	opR(OP_SETSP, 0, 6);
	opR(OP_MOVSP, 7, 0);
	b(OP_HALT);
	go(NULL, FLAG_Z | FLAG_C);
	CHECK(cpu.registers[2] == 0x11223344);
	CHECK(cpu.registers[3] == MEMORY_SIZE - 4);
	CHECK(cpu.registers[4] == 0x11223340);
	CHECK(cpu.registers[5] == MEMORY_SIZE - 16);
	CHECK(cpu.registers[7] == 0x8000 && cpu.SP == 0x8000);
	CHECK(cpu.memory[MEMORY_SIZE - 12] == 0x44);
	CHECK((cpu.flags & 0xF) == (FLAG_Z | FLAG_C));
	start();
	opRI(OP_LEAPC, 3, 0x10);
	opRI(OP_LEAPC, 4, (uint32_t)-12);
	b(OP_HALT);
	go(regs, 0);
	CHECK(cpu.registers[3] == 0x16);
	CHECK(cpu.registers[4] == 0);
}

static void test_misc(void) {
	uint32_t regs[8] = {0};
	start();
	opR(OP_CPUID, 3, 0);
	opR(OP_RDCYC, 4, 0);
	b(OP_NOP);
	b(OP_NOP);
	opR(OP_RDCYC, 5, 0);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.registers[3] == 2);
	CHECK(cpu.registers[4] == 1);
	CHECK(cpu.registers[5] == 4);
	for (unsigned i = 0; i < NEDGE; i++)
		for (unsigned j = 0; j < NEDGE; j++) {
			uint32_t a = edges[i], bb = edges[j], r;
			uint8_t f;
			rr(OP_TEST, a, bb, FLAG_C | FLAG_V, &r, &f);
			CHECKV(r == a && f == zs(a & bb), a, bb);
			ri(OP_TESTI, a, bb, FLAG_C | FLAG_V, &r, &f);
			CHECKV(r == a && f == zs(a & bb), a, bb);
		}
	start();
	b(OP_PUSHF);
	opR(OP_POP32, 1, 0);
	load32(2, FLAG_Z | FLAG_V);
	opR(OP_PUSH32, 0, 2);
	b(OP_POPF);
	b(OP_PUSHF);
	opR(OP_POP32, 3, 0);
	b(OP_HALT);
	go(NULL, FLAG_C | FLAG_S);
	CHECK(cpu.registers[1] == (FLAG_C | FLAG_S | FLAG_I));
	CHECK(cpu.registers[3] == (FLAG_Z | FLAG_V));
	CHECK(cpu.int_enabled == 0 && cpu.mode == MODE_SUPERVISOR);
	start();
	load32(2, FLAG_Z | FLAG_U);
	opR(OP_PUSH32, 0, 2);
	b(OP_POPF);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.mode == MODE_USER && cpu.int_enabled == 0 && (cpu.flags & 0xF) == FLAG_Z);
	start();
	load32(2, FLAG_C | FLAG_I);
	opR(OP_PUSH32, 0, 2);
	b(OP_SYSRET);
	b(OP_POPF);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.mode == MODE_USER);
	start();
	b(OP_SYSRET);
	load32(2, FLAG_C);
	opR(OP_PUSH32, 0, 2);
	b(OP_POPF);
	b(OP_HALT);
	cpu_init();
	load_program(prog, plen);
	cpu_run();
	CHECK(cpu.mode == MODE_USER && cpu.int_enabled == 1 && (cpu.flags & 0xF) == FLAG_C);
	start();
	load32(1, 0x1234);
	opR(OP_MTCR, CR_EVT, 1);
	opR(OP_MFCR, 2, CR_EVT);
	opR(OP_MTCR, CR_RAMSIZE, 1);
	opR(OP_MFCR, 3, CR_RAMSIZE);
	opR(OP_MTCR, CR_KSP, 1);
	opR(OP_MFCR, 4, CR_KSP);
	opR(OP_MFCR, 5, CR_CTRL);
	b(OP_TLBFLUSH);
	opR(OP_INVLPG, 0, 1);
	b(OP_HALT);
	go(regs, 0);
	CHECK(cpu.registers[2] == 0x1234);
	CHECK(cpu.registers[3] == MEMORY_SIZE);
	CHECK(cpu.registers[4] == 0x1234);
	CHECK(cpu.registers[5] == 0);
	CHECK(cpu.exit_code == 0);
	start();
	opR(OP_MFCR, 2, 9);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.exit_code == EXIT_INVALID);
	start();
	opR(OP_MTCR, 15, 1);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.exit_code == EXIT_INVALID);
	start();
	b(0x7B);
	go(NULL, 0);
	CHECK(cpu.exit_code == EXIT_INVALID);
	static const uint8_t badregs[][2] = {
		{OP_ADC, 0x81}, {OP_ADC, 0x18}, {OP_JMPR, 0x08}, {OP_NEG, 0x80}, {OP_MOVSP, 0x80},
		{OP_SETSP, 0x08}, {OP_PUSH32, 0x09}, {OP_POP32, 0xF0}, {OP_CPUID, 0x80}, {OP_RDCYC, 0x90},
		{OP_CALLR, 0x0F}, {OP_INVLPG, 0x08}
	};
	for (unsigned i = 0; i < sizeof(badregs) / sizeof(badregs[0]); i++) {
		start();
		b(badregs[i][0]);
		b(badregs[i][1]);
		b(OP_HALT);
		go(NULL, 0);
		CHECKV(cpu.exit_code == EXIT_INVALID, badregs[i][0], badregs[i][1]);
	}
	start();
	opM(OP_LDW, 8, 1, 0);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.exit_code == EXIT_INVALID);
	start();
	opM(OP_LDW, 1, 9, 0);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.exit_code == EXIT_INVALID);
	start();
	opM(OP_STW, 8, 1, 0);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.exit_code == EXIT_INVALID);
	start();
	opRI(OP_SAR, 8, 1);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.exit_code == EXIT_INVALID);
	start();
	opRI(OP_LEAPC, 9, 1);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.exit_code == EXIT_INVALID);
	start();
	load32(0, 0x40);
	b(OP_STORE);
	b(0);
	b((IVT_BASE + 6) & 0xFF);
	b((IVT_BASE + 6) >> 8);
	b(OP_BRK);
	b(OP_HALT);
	while (plen < 0x40) b(OP_NOP);
	load32(7, 0xB4);
	b(OP_HALT);
	go(NULL, 0);
	CHECK(cpu.registers[7] == 0xB4);
	CHECK(cpu.memory[MEMORY_SIZE - 4] == 11);
}

static void test_dis(void) {
	char buf[64];
	uint8_t m[8];
	struct { uint8_t b[6]; const char *s; uint32_t at; } cases[] = {
		{{OP_BEQ, 0, 0xFA, 0xFF, 0xFF, 0xFF}, "BEQ 0x00000100", 0x100},
		{{OP_LDW, 0x18, 0x0C, 0, 0, 0}, "LDW R1, [SP + 12]", 0},
		{{OP_STB, 0x32, 0x01, 0, 0, 0}, "STB [R2 + 1], R3", 0},
		{{OP_LDH, 0x45, 0xFC, 0xFF, 0xFF, 0xFF}, "LDH R4, [R5 - 4]", 0},
		{{OP_MFCR, 0x12, 0, 0, 0, 0}, "MFCR R1, CR2", 0},
		{{OP_MTCR, 0x31, 0, 0, 0, 0}, "MTCR CR3, R1", 0},
		{{OP_ADDSP, 0, 0xF0, 0xFF, 0xFF, 0xFF}, "ADDSP -16", 0},
		{{OP_ADC, 0x12, 0, 0, 0, 0}, "ADC R1, R2", 0},
		{{OP_SAR, 0x30, 5, 0, 0, 0}, "SAR R3, 5", 0},
		{{OP_JMPR, 0x04, 0, 0, 0, 0}, "JMPR R4", 0},
		{{OP_POP32, 0x60, 0, 0, 0, 0}, "POP32 R6", 0},
		{{OP_LEAPC, 0x20, 0x10, 0, 0, 0}, "LEAPC R2, 0x00000016", 0},
		{{OP_WFI, 0, 0, 0, 0, 0}, "WFI", 0},
		{{OP_ADD32R, 0x12, 0, 0, 0, 0}, "ADD32 R1, R2", 0},
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(m, 0, sizeof(m));
		memcpy(m, cases[i].b, 6);
		int n = dis_format(cases[i].at, m, buf, sizeof(buf));
		CHECKV(strcmp(buf, cases[i].s) == 0, i, n);
		if (strcmp(buf, cases[i].s)) fprintf(stderr, "  got '%s'\n", buf);
	}
	for (int op = 0x41; op <= 0x8D; op++) {
		if (op == 0x7B) continue;
		CHECKV(dis_ops[op].name != NULL, op, 0);
	}
}

int main(void) {
	test_addsub();
	test_neg();
	test_shifts();
	test_mul();
	test_div();
	test_branches();
	test_jumps_calls();
	test_mem();
	test_misc();
	test_dis();
	printf("test_p2: %d checks, %d failures\n", checks, fails);
	return fails != 0;
}
