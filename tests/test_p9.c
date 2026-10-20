#include "tutil.h"
#include "opts.h"

#define RAM 0x100000

static const char *prog =
	".equ EVT, 0x1000\n"
	"CLI\n"
	"LOAD32 R0, EVT\n"
	"MTCR CR1, R0\n"
	"LOAD32 R0, h_t\n"
	"LOAD32 R7, EVT\n"
	"STW [R7 + 128], R0\n"
	"LOAD32 R0, 1\n"
	"MTCR CR0, R0\n"
	"LOAD32 R6, 0xF0000000\n"
	"STW [R6 + 4], R0\n"
	"LOAD32 R6, 0xF0001000\n"
	"LOAD32 R0, 7\n"
	"STW [R6 + 12], R0\n"
	"LOAD32 R0, 3\n"
	"STW [R6 + 4], R0\n"
	"LOAD32 R0, 1\n"
	"STW [R6 + 8], R0\n"
	"LOAD32 R5, 0\n"
	"LOAD32 R1, 0x8000\n"
	"LOAD32 R2, 12345\n"
	"STI\n"
	"loop:\n"
	"IMULI R2, 1103515245\n"
	"ADD32 R2, 12345\n"
	"STW [R1], R2\n"
	"ADD32 R1, 4\n"
	"CMP32 R1, 0x10000\n"
	"BLO loop\n"
	"CLI\n"
	"LOAD32 R6, 0xF0007000\n"
	"LOAD32 R0, 0\n"
	"STW [R6], R0\n"
	"h_t:\n"
	"ADD32 R5, 1\n"
	"LOAD32 R4, 0xF0000000\n"
	"PUSH32 R0\n"
	"LOAD32 R0, 1\n"
	"STW [R4 + 8], R0\n"
	"POP32 R0\n"
	"IRET\n";

static unsigned hash_mem(void) {
	unsigned h = 2166136261u;
	for (uint32_t i = 0; i < cpu.ram_size; i++) h = (h ^ cpu.memory[i]) * 16777619u;
	return h;
}

typedef struct {
	uint32_t regs[8], pc, sp, flags, h;
	uint64_t ic;
	int code;
} Snap;

static void snap(Snap *s) {
	memcpy(s->regs, cpu.registers, sizeof(s->regs));
	s->pc = cpu.PC;
	s->sp = cpu.SP;
	s->flags = cpu_get_flags();
	s->h = hash_mem();
	s->ic = cpu.icount;
	s->code = cpu.exit_code;
}

static int same(Snap *a, Snap *b) {
	return !memcmp(a->regs, b->regs, sizeof(a->regs)) && a->pc == b->pc && a->sp == b->sp && a->flags == b->flags && a->h == b->h && a->ic == b->ic && a->code == b->code;
}

static void resume(void) {
	cpu.halted = 0;
	cpu.exit_code = 0;
	cpu.ilimit = UINT64_MAX;
	dev_next = 0;
}

static void test_state(void) {
	Snap full, cont, reload;
	CHECK(asm_load(prog, RAM) == 0);
	cpu_run();
	CHECK(cpu.exit_code == 0);
	CHECK(cpu.registers[5] > 10);
	snap(&full);
	CHECK(asm_load(prog, RAM) == 0);
	cpu.ilimit = 20000;
	dev_next = 0;
	cpu_run();
	CHECK(cpu.exit_code == EXIT_LIMIT && cpu.icount == 20000);
	resume();
	CHECK(cpu_save("tests/p9.tmp.st") == 0);
	cpu_run();
	snap(&cont);
	CHECK(same(&full, &cont));
	cpu_init();
	CHECK(cpu_load("tests/p9.tmp.st") == 0);
	CHECK(cpu.icount == 20000);
	cpu_run();
	snap(&reload);
	CHECK(same(&full, &reload));
	CHECK(cpu_load("tests/does_not_exist.st") != 0);
	FILE *f = fopen("tests/p9.tmp.st", "wb");
	fputs("XXXX", f);
	fclose(f);
	CHECK(cpu_load("tests/p9.tmp.st") != 0);
	remove("tests/p9.tmp.st");
}

static void test_trace(void) {
	char line[256];
	FILE *f;
	CHECK(asm_load("LOAD32 R1, 5\nADD32 R1, R1\nPUSH32 R1\nCMP32 R1, 10\nNOP\nHALT\n", RAM) == 0);
	f = fopen("tests/p9.tmp.trace", "w");
	cpu_set_trace(f);
	cpu_run();
	cpu_set_trace(NULL);
	fclose(f);
	f = fopen("tests/p9.tmp.trace", "r");
	static const char *dis[] = {"LOAD32 R1, 0x00000005", "ADD32 R1, R1", "PUSH32 R1", "CMP32 R1, 0x0000000A", "NOP", "HALT"};
	static const char *chg[] = {" R1=00000005", " R1=0000000A", " SP=000FFFFC", " FLAGS=11", "", ""};
	static const unsigned pcs[] = {0, 6, 8, 0xA, 0x10, 0x11};
	for (int i = 0; i < 6; i++) {
		char want[256];
		snprintf(want, sizeof(want), "%d %08X %-28s%s\n", i, pcs[i], dis[i], chg[i]);
		if (!fgets(line, sizeof(line), f)) line[0] = 0;
		if (strcmp(line, want)) {
			fails++;
			fprintf(stderr, "trace line %d: got '%s' want '%s'\n", i, line, want);
		}
		checks++;
	}
	CHECK(!fgets(line, sizeof(line), f));
	fclose(f);
	remove("tests/p9.tmp.trace");
}

static void test_dcache(void) {
	Snap a, b;
	static const char *smc =
		"LOAD32 R1, patch\n"
		"LOAD32 R3, 0\n"
		"again:\n"
		"patch:\n"
		"LOAD32 R2, 1\n"
		"ADD32 R3, R2\n"
		"LDW R4, [R1 + 2]\n"
		"ADD32 R4, 1\n"
		"STW [R1 + 2], R4\n"
		"CMP32 R4, 50\n"
		"BLO again\n"
		"HALT\n";
	for (int round = 0; round < 2; round++) {
		const char *src = round ? smc : prog;
		cpu_set_dcache(1);
		CHECK(asm_load(src, RAM) == 0);
		cpu_run();
		snap(&a);
		cpu_set_dcache(0);
		CHECK(asm_load(src, RAM) == 0);
		cpu_run();
		snap(&b);
		cpu_set_dcache(1);
		CHECK(same(&a, &b));
		if (round) CHECK(a.regs[3] == 1225);
	}
}

int main(void) {
	test_state();
	test_trace();
	test_dcache();
	return report("test_p9");
}
