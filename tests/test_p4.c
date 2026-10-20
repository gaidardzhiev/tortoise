#include "tutil.h"

#define RAM 0x400000
#define PD 0x100000
#define OBS 0x2000
#define EVT 0x1000
#define KSP 0x8000

static uint32_t pt_next;

static void wr32(uint32_t a, uint32_t v) {
	cpu.memory[a] = v;
	cpu.memory[a + 1] = v >> 8;
	cpu.memory[a + 2] = v >> 16;
	cpu.memory[a + 3] = v >> 24;
}

static uint32_t pte_addr(uint32_t va) {
	uint32_t pde = rd32(PD + (va >> 22) * 4);
	if (!(pde & 1)) {
		pde = pt_next | 1;
		pt_next += 0x1000;
		memset(cpu.memory + (pde & ~0xFFFu), 0, 0x1000);
		wr32(PD + (va >> 22) * 4, pde);
	}
	return (pde & ~0xFFFu) + ((va >> 12) & 0x3FF) * 4;
}

static void map(uint32_t va, uint32_t pa, uint32_t fl) {
	wr32(pte_addr(va), (pa & ~0xFFFu) | fl);
}

static uint32_t pte(uint32_t va) {
	return rd32(pte_addr(va));
}

static const char *pro =
	".equ OBS, 0x2000\n"
	".equ SYS, 0xF0007000\n"
	".macro poweroff code\n"
	"LOAD32 R6, SYS\n"
	"LOAD32 R0, \\code\n"
	"STW [R6], R0\n"
	"HALT\n"
	".endm\n"
	".macro touser entry, usp\n"
	"LOAD32 R0, \\usp\n"
	"PUSH32 R0\n"
	"LOAD32 R0, 0x20\n"
	"PUSH32 R0\n"
	"LOAD32 R0, \\entry\n"
	"PUSH32 R0\n"
	"IRET\n"
	".endm\n"
	"LOAD32 R3, OBS\n"
	"JMP32 main\n"
	".org 0x40\n"
	"h_fault:\n"
	"MFCR R4, CR2\n"
	"STW [R3 + 0x100], R4\n"
	"MFCR R4, CR7\n"
	"STW [R3 + 0x104], R4\n"
	"LDW R4, [SP]\n"
	"STW [R3 + 0x108], R4\n"
	"LOAD32 R6, SYS\n"
	"STW [R6], R4\n"
	"HALT\n"
	"main:\n";

static char src[16384];

static int load(const char *body, int paging) {
	snprintf(src, sizeof(src), "%s%s", pro, body);
	if (asm_load(src, RAM)) return -1;
	for (int v = 0; v < 64; v++) wr32(EVT + 4 * v, 0x40);
	cpu.cr[CR_EVT] = EVT;
	cpu.cr[CR_KSP] = KSP;
	if (paging) {
		pt_next = PD + 0x1000;
		memset(cpu.memory + PD, 0, 0x1000);
		for (uint32_t a = 0; a < 0x100000; a += 0x1000) map(a, a, PTE_P | PTE_W | PTE_X);
		for (uint32_t a = PD; a < PD + 0x10000; a += 0x1000) map(a, a, PTE_P | PTE_W);
		map(0xF0007000, 0xF0007000, PTE_P | PTE_W);
		cpu.cr[CR_PTBASE] = PD;
		cpu.SP = 0x90000;
		cpu.cr[CR_CTRL] = CR0_EXT | CR0_PG;
	} else {
		cpu.cr[CR_CTRL] = CR0_EXT;
	}
	cpu_flush_tlb();
	return 0;
}

static uint32_t f_addr(void) { return rd32(OBS + 0x100); }
static uint32_t f_cause(void) { return rd32(OBS + 0x104); }
static uint32_t f_pc(void) { return rd32(OBS + 0x108); }

static void test_base_limit(void) {
	static const char *user =
		"LOAD32 R1, 0x100\n"
		"LDW R2, [R1]\n"
		"STW [R1 + 4], R2\n"
		"MOVSP R2\n"
		"STW [R1 + 8], R2\n"
		"PUSH32 R1\n"
		"LOAD32 R1, LIMADDR\n"
		"uf:\n"
		"ACCESS\n"
		"poweroff 1\n";
	static const struct { const char *access; uint32_t addr; uint32_t fa; } cases[] = {
		{"LDW R2, [R1]", 0x2000, 0x2000},
		{"LDW R2, [R1]", 0x1FFE, 0x2000},
		{"LDB R2, [R1]", 0x7FFFFFFF, 0x7FFFFFFF},
		{"STB [R1], R2", 0x2000, 0x2000},
		{"JMPR R1", 0x3000, 0x3000},
	};
	char ubuf[2048], usrc[4096];
	FILE *f;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		snprintf(ubuf, sizeof(ubuf), "%s.equ LIMADDR, 0x%X\n.macro ACCESS\n%s\n.endm\n%s", ".equ SYS, 0xF0007000\n.macro poweroff code\nLOAD32 R6, SYS\nLOAD32 R0, \\code\nSTW [R6], R0\nHALT\n.endm\n", cases[i].addr, cases[i].access, user);
		snprintf(usrc, sizeof(usrc), "%s", ubuf);
		CHECK(asm_build(usrc, "tests/unit.tmp.user") == 0);
		CHECK(load("LOAD32 R0, 0x40000\nMTCR CR5, R0\nLOAD32 R0, 0x2000\nMTCR CR6, R0\nLOAD32 R0, 3\nMTCR CR0, R0\ntouser 0, 0x2000\n", 0) == 0);
		f = fopen("tests/unit.tmp.user", "rb");
		size_t n = fread(cpu.memory + 0x40000, 1, 0x2000, f);
		fclose(f);
		(void)n;
		wr32(0x40100, 0xFEEDF00D);
		cpu_run();
		CHECKV(cpu.exit_code == (int)(f_pc() & 0xFF), i, cpu.exit_code);
		CHECKV(f_cause() == 13, i, f_cause());
		CHECKV(f_addr() == cases[i].fa, i, f_addr());
		CHECK(rd32(0x40104) == 0xFEEDF00D);
		CHECK(rd32(0x40108) == 0x2000);
		CHECK(rd32(0x40000 + 0x2000 - 4) == 0x100);
		if (i == 4) CHECK(f_pc() == 0x3000);
		remove("tests/unit.tmp.user");
	}
	CHECK(load("LOAD32 R0, 0x40000\nMTCR CR5, R0\nLOAD32 R0, 0x1000\nMTCR CR6, R0\nLOAD32 R0, 3\nMTCR CR0, R0\nLOAD32 R1, 0x3000\nLOAD32 R2, 0x77\nSTW [R1], R2\npoweroff 0\n", 0) == 0);
	cpu_run();
	CHECK(cpu.exit_code == 0);
	CHECK(rd32(0x3000) == 0x77);
	CHECK(rd32(0x43000) == 0);
}

static void pf_case2(const char *body, uint32_t va, int user, uint32_t want_code, int idx, uint32_t fa) {
	char b[4096];
	if (user) snprintf(b, sizeof(b), "touser uc, 0x9000\nuc:\nLOAD32 R1, 0x%X\nLOAD32 R2, 0x55\nacc:\n%s\npoweroff 1\n", va, body);
	else snprintf(b, sizeof(b), "LOAD32 R1, 0x%X\nLOAD32 R2, 0x55\nacc:\n%s\npoweroff 1\n", va, body);
	CHECK(load(b, 1) == 0);
	map(0x00400000, 0x200000, PTE_P | PTE_W | PTE_U | PTE_X);
	map(0x00401000, 0x201000, PTE_P | PTE_W);
	map(0x00402000, 0x202000, PTE_P | PTE_U);
	map(0x00403000, 0x203000, PTE_P | PTE_W | PTE_U);
	map(0x00404000, 0x204000, PTE_W | PTE_U | PTE_X);
	map(0x00000000, 0x000000, PTE_P | PTE_W | PTE_X | PTE_U);
	map(0x00009000 - 0x1000, 0x8000, PTE_P | PTE_W | PTE_U);
	map(0x00005000, 0x005000, PTE_P | PTE_W | PTE_U);
	map(0x00006000, 0x006000, PTE_P | PTE_W | PTE_X);
	cpu_run();
	CHECKV(f_cause() == (VEC_PF | want_code << 8), idx, f_cause());
	CHECKV(f_addr() == fa, idx, f_addr());
	if (cpu.exit_code != (int)(f_pc() & 0xFF)) CHECKV(0, idx, cpu.exit_code);
}

static void pf_case(const char *body, uint32_t va, int user, uint32_t want_code, int idx) {
	pf_case2(body, va, user, want_code, idx, va);
}

static void test_pf_codes(void) {
	pf_case("LDW R2, [R1]", 0x00800000, 0, 0, 0);
	pf_case("STW [R1], R2", 0x00800000, 0, PF_W, 1);
	pf_case("LDW R2, [R1]", 0x00410000, 0, 0, 2);
	pf_case("LDW R2, [R1]", 0x00404000, 0, 0, 3);
	pf_case("STB [R1], R2", 0x00404000, 0, PF_W, 4);
	pf_case("LDW R2, [R1]", 0x00404000, 1, PF_U, 5);
	pf_case("STH [R1], R2", 0x00404000, 1, PF_U | PF_W, 6);
	pf_case("JMPR R1", 0x00404000, 0, PF_I, 7);
	pf_case("JMPR R1", 0x00404000, 1, PF_I | PF_U, 8);
	pf_case("LDW R2, [R1]", 0x00401000, 1, PF_P | PF_U, 9);
	pf_case("STW [R1], R2", 0x00401000, 1, PF_P | PF_U | PF_W, 10);
	pf_case("JMPR R1", 0x00401000, 1, PF_P | PF_U | PF_I, 11);
	pf_case("STW [R1], R2", 0x00402000, 0, PF_P | PF_W, 12);
	pf_case("STW [R1], R2", 0x00402000, 1, PF_P | PF_W | PF_U, 13);
	pf_case("JMPR R1", 0x00401000, 0, PF_P | PF_I, 14);
	pf_case("JMPR R1", 0x00403000, 1, PF_P | PF_I | PF_U, 15);
	pf_case("JMPR R1", 0x00005000, 0, PF_P | PF_I, 16);
	pf_case("LDW R2, [R1]", 0x00006000, 1, PF_P | PF_U, 17);
	pf_case2("LDW R2, [R1 + 0xFFE]", 0x00403000, 0, 0, 18, 0x00404000);
	pf_case2("LDW R2, [R1 - 2]", 0x00404000, 0, 0, 19, 0x00404000);
	pf_case2("STW [R1 - 2], R2", 0x00402000, 0, PF_P | PF_W, 20, 0x00402000);
	CHECK(rd32(0x202000) == 0 && cpu.memory[0x201FFF] == 0);
	CHECK(load("LOAD32 R1, 0x00400000\nLDW R2, [R1]\nLOAD32 R1, 0x00403000\nLDB R2, [R1]\nLOAD32 R1, 0x00402000\nLDW R2, [R1]\npoweroff 0\n", 1) == 0);
	map(0x00400000, 0x200000, PTE_P | PTE_W | PTE_U);
	map(0x00403000, 0x203000, PTE_P | PTE_W | PTE_U);
	map(0x00402000, 0x202000, PTE_P | PTE_U);
	cpu_run();
	CHECK(cpu.exit_code == 0);
}

static void test_ad_bits(void) {
	CHECK(load(
		"LOAD32 R1, 0x00400000\n"
		"LDW R2, [R1]\n"
		"LOAD32 R1, 0x00401000\n"
		"STW [R1], R2\n"
		"LOAD32 R1, 0x00402000\n"
		"LDW R2, [R1]\n"
		"STB [R1 + 5], R2\n"
		"poweroff 0\n", 1) == 0);
	map(0x00400000, 0x200000, PTE_P | PTE_W);
	map(0x00401000, 0x201000, PTE_P | PTE_W);
	map(0x00402000, 0x202000, PTE_P | PTE_W);
	map(0x00403000, 0x203000, PTE_P | PTE_W);
	cpu_run();
	CHECK(cpu.exit_code == 0);
	CHECK((pte(0x00400000) & (PTE_A | PTE_D)) == PTE_A);
	CHECK((pte(0x00401000) & (PTE_A | PTE_D)) == (PTE_A | PTE_D));
	CHECK((pte(0x00402000) & (PTE_A | PTE_D)) == (PTE_A | PTE_D));
	CHECK((pte(0x00403000) & (PTE_A | PTE_D)) == 0);
	CHECK(pte(0x00000000) & PTE_A);
	CHECK(!(pte(0x00000000) & PTE_D));
}

static void test_tlb(void) {
	static const char *body =
		"LOAD32 R1, 0x00400000\n"
		"LDW R2, [R1]\n"
		"STW [R3], R2\n"
		"BRK\n"
		"LDW R2, [R1]\n"
		"STW [R3 + 4], R2\n"
		"FLUSHOP\n"
		"LDW R2, [R1]\n"
		"STW [R3 + 8], R2\n"
		"BRK\n"
		"LDW R2, [R1]\n"
		"STW [R3 + 12], R2\n"
		"FLUSHOP\n"
		"LDW R2, [R1]\n"
		"poweroff 0\n"
		".org 0x200\n"
		"IRET\n";
	static const char *flushes[] = {"INVLPG R1", "TLBFLUSH", "MFCR R0, CR3\nMTCR CR3, R0", "MFCR R0, CR0\nMTCR CR0, R0"};
	char b[4096];
	for (unsigned i = 0; i < 4; i++) {
		snprintf(b, sizeof(b), ".macro FLUSHOP\n%s\n.endm\n%s", flushes[i], body);
		CHECK(load(b, 1) == 0);
		map(0x00400000, 0x200000, PTE_P | PTE_W);
		wr32(0x200000, 0x11111111);
		wr32(0x300000, 0x22222222);
		wr32(EVT + 3 * 4, 0x200);
		uint32_t pa = pte_addr(0x00400000);
		int step = 0;
		while (!cpu.halted) {
			cpu_step();
			if (cpu.PC == 0x200 && step == 0) {
				wr32(pa, 0x300000 | PTE_P | PTE_W);
				step = 1;
			} else if (cpu.PC == 0x200 && step == 1) {
				wr32(pa, 0x300000);
				step = 2;
			}
		}
		CHECK(step == 2);
		CHECKV(rd32(OBS) == 0x11111111, i, rd32(OBS));
		CHECKV(rd32(OBS + 4) == 0x11111111, i, rd32(OBS + 4));
		CHECKV(rd32(OBS + 8) == 0x22222222, i, rd32(OBS + 8));
		CHECKV(rd32(OBS + 12) == 0x22222222, i, rd32(OBS + 12));
		CHECKV(f_cause() == VEC_PF, i, f_cause());
		CHECKV(f_addr() == 0x00400000, i, f_addr());
	}
	CHECK(load(
		"LOAD32 R1, 0x00400000\n"
		"LOAD32 R4, 0x00401000\n"
		"LDW R2, [R1]\n"
		"STW [R3], R2\n"
		"BRK\n"
		"INVLPG R4\n"
		"LDW R2, [R1]\n"
		"STW [R3 + 4], R2\n"
		"INVLPG R1\n"
		"LDW R2, [R1]\n"
		"STW [R3 + 8], R2\n"
		"poweroff 0\n"
		".org 0x200\n"
		"IRET\n", 1) == 0);
	map(0x00400000, 0x200000, PTE_P | PTE_W);
	wr32(0x200000, 0x11111111);
	wr32(0x300000, 0x22222222);
	wr32(EVT + 3 * 4, 0x200);
	{
		uint32_t pa = pte_addr(0x00400000);
		while (!cpu.halted) {
			cpu_step();
			if (cpu.PC == 0x200) wr32(pa, 0x300000 | PTE_P | PTE_W);
		}
	}
	CHECK(cpu.exit_code == 0);
	CHECK(rd32(OBS) == 0x11111111);
	CHECK(rd32(OBS + 4) == 0x11111111);
	CHECK(rd32(OBS + 8) == 0x22222222);
}

static void test_restart(void) {
	CHECK(load(
		"LOAD32 R0, hpf\n"
		"LOAD32 R4, 0x1000\n"
		"STW [R4 + 56], R0\n"
		"LOAD32 R7, 0\n"
		"LOAD32 R1, 0x00407000\n"
		"LOAD32 R2, 0xABCD1234\n"
		"STW [R1 + 4], R2\n"
		"LDW R3, [R1 + 4]\n"
		"LOAD32 R4, OBS\n"
		"STW [R4], R3\n"
		"STW [R4 + 4], R7\n"
		"poweroff 0\n"
		"hpf:\n"
		"LOAD32 R5, 0x00206003\n"
		"STW [R6], R5\n"
		"MFCR R4, CR2\n"
		"INVLPG R4\n"
		"ADD32 R7, 1\n"
		"IRET\n", 1) == 0);
	map(0x00400000, 0x200000, PTE_P | PTE_W);
	cpu.registers[6] = pte_addr(0x00407000);
	cpu_run();
	CHECK(cpu.exit_code == 0);
	CHECK(rd32(OBS) == 0xABCD1234);
	CHECK(rd32(OBS + 4) == 1);
	CHECK(rd32(0x206004) == 0xABCD1234);
	CHECK((pte(0x00407000) & (PTE_A | PTE_D)) == (PTE_A | PTE_D));
}

int main(void) {
	test_base_limit();
	test_pf_codes();
	test_ad_bits();
	test_tlb();
	test_restart();
	return report("test_p4");
}
