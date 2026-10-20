#include "tutil.h"

#define OBS 0x2000
#define RAM 0x100000

static const char *pro =
	".equ EVT, 0x1000\n"
	".equ OBS, 0x2000\n"
	".equ SYS, 0xF0007000\n"
	".equ PIC, 0xF0000000\n"
	".equ TIMER, 0xF0001000\n"
	".macro setvec n, h\n"
	"LOAD32 R0, \\h\n"
	"LOAD32 R7, EVT\n"
	"STW [R7 + 4*\\n], R0\n"
	".endm\n"
	".macro poweroff code\n"
	"LOAD32 R6, SYS\n"
	"LOAD32 R0, \\code\n"
	"STW [R6], R0\n"
	"HALT\n"
	".endm\n"
	".macro touser entry, usp\n"
	"LOAD32 R0, \\usp\n"
	"PUSH32 R0\n"
	"LOAD32 R0, 0x30\n"
	"PUSH32 R0\n"
	"LOAD32 R0, \\entry\n"
	"PUSH32 R0\n"
	"IRET\n"
	".endm\n"
	"LOAD32 R0, EVT\n"
	"MTCR CR1, R0\n"
	"LOAD32 R0, 0x8000\n"
	"MTCR CR4, R0\n"
	"LOAD32 R0, 1\n"
	"MTCR CR0, R0\n"
	"LOAD32 R3, OBS\n";

static char src[16384];

static void build(const char *body) {
	snprintf(src, sizeof(src), "%s%s", pro, body);
}

static void test_sup_frame(void) {
	build(
		"setvec 3, h_brk\n"
		"LOAD32 R1, 5\n"
		"CMP32 R1, 5\n"
		"MOVSP R2\n"
		"STW [R3], R2\n"
		"BRK\n"
		"after_brk:\n"
		"MOVSP R2\n"
		"STW [R3 + 4], R2\n"
		"PUSHF\n"
		"POP32 R2\n"
		"STW [R3 + 8], R2\n"
		"LOAD32 R2, after_brk\n"
		"STW [R3 + 32], R2\n"
		"poweroff 0\n"
		"h_brk:\n"
		"MOVSP R4\n"
		"STW [R3 + 12], R4\n"
		"LDW R4, [SP]\n"
		"STW [R3 + 16], R4\n"
		"LDW R4, [SP + 4]\n"
		"STW [R3 + 20], R4\n"
		"MFCR R4, CR7\n"
		"STW [R3 + 24], R4\n"
		"PUSHF\n"
		"POP32 R4\n"
		"STW [R3 + 28], R4\n"
		"IRET\n");
	CHECK(asm_run(src, RAM) == 0);
	CHECK(rd32(OBS) == RAM);
	CHECK(rd32(OBS + 12) == RAM - 8);
	CHECK(rd32(OBS + 16) == rd32(OBS + 32));
	CHECK(rd32(OBS + 20) == (FLAG_Z | FLAG_I));
	CHECK(rd32(OBS + 24) == 3);
	CHECK(rd32(OBS + 28) == FLAG_Z);
	CHECK(rd32(OBS + 4) == RAM);
	CHECK(rd32(OBS + 8) == (FLAG_Z | FLAG_I));
}

static void test_user_frame(void) {
	build(
		"setvec 3, h_brk\n"
		"setvec 48, h_sys\n"
		"touser ucode, 0x9000\n"
		"ucode:\n"
		"MOVSP R2\n"
		"STW [R3], R2\n"
		"BRK\n"
		"ub:\n"
		"MOVSP R2\n"
		"STW [R3 + 4], R2\n"
		"PUSHF\n"
		"POP32 R2\n"
		"STW [R3 + 40], R2\n"
		"SYSCALL\n"
		"us:\n"
		"poweroff 1\n"
		"h_brk:\n"
		"MOVSP R4\n"
		"STW [R3 + 8], R4\n"
		"LDW R4, [SP]\n"
		"STW [R3 + 12], R4\n"
		"LDW R4, [SP + 4]\n"
		"STW [R3 + 16], R4\n"
		"LDW R4, [SP + 8]\n"
		"STW [R3 + 20], R4\n"
		"LOAD32 R4, ub\n"
		"STW [R3 + 24], R4\n"
		"PUSHF\n"
		"POP32 R4\n"
		"STW [R3 + 44], R4\n"
		"IRET\n"
		"h_sys:\n"
		"LDW R4, [SP]\n"
		"STW [R3 + 28], R4\n"
		"LOAD32 R4, us\n"
		"STW [R3 + 32], R4\n"
		"MFCR R4, CR7\n"
		"STW [R3 + 36], R4\n"
		"poweroff 0\n");
	CHECK(asm_run(src, RAM) == 0);
	CHECK(rd32(OBS) == 0x9000);
	CHECK(rd32(OBS + 8) == 0x8000 - 12);
	CHECK(rd32(OBS + 12) == rd32(OBS + 24));
	CHECK(rd32(OBS + 16) == (FLAG_U | FLAG_I));
	CHECK(rd32(OBS + 20) == 0x9000);
	CHECK(rd32(OBS + 44) == 0);
	CHECK(rd32(OBS + 4) == 0x9000);
	CHECK(rd32(OBS + 40) == (FLAG_U | FLAG_I));
	CHECK(rd32(OBS + 28) == rd32(OBS + 32));
	CHECK(rd32(OBS + 36) == 48);
}

static void test_privileged(void) {
	static const char *ins[] = {
		"CLI", "STI", "HALT", "IN R0, 0xFF01", "OUT R0, 0xFF00", "MFCR R0, CR0", "MTCR CR3, R0",
		"TLBFLUSH", "INVLPG R0", "WFI", "IRET", "SYSRET", "INT 5", "INT 47", "INT 64", "INT 0",
	};
	static const char *ok[] = {"INT 48", "INT 63", "BRK", "SYSCALL", "INT 50"};
	static const int okvec[] = {48, 63, 3, 48, 50};
	char body[4096];
	for (unsigned i = 0; i < sizeof(ins) / sizeof(ins[0]); i++) {
		snprintf(body, sizeof(body),
			"setvec 13, h_gp\n"
			"touser ucode, 0x9000\n"
			"ucode:\n"
			"LOAD32 R0, 0\n"
			"xi:\n"
			"%s\n"
			"poweroff 1\n"
			"h_gp:\n"
			"LDW R4, [SP]\n"
			"STW [R3], R4\n"
			"LOAD32 R4, xi\n"
			"STW [R3 + 4], R4\n"
			"MFCR R4, CR7\n"
			"STW [R3 + 8], R4\n"
			"LDW R4, [SP + 4]\n"
			"STW [R3 + 12], R4\n"
			"poweroff 13\n", ins[i]);
		build(body);
		int rc = asm_run(src, RAM);
		CHECKV(rc == 13, i, rc);
		CHECKV(rd32(OBS) == rd32(OBS + 4), i, rd32(OBS));
		CHECK(rd32(OBS + 8) == 13);
		CHECK(rd32(OBS + 12) & FLAG_U);
	}
	for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
		snprintf(body, sizeof(body),
			"setvec 13, h_gp\n"
			"setvec %d, h_ok\n"
			"touser ucode, 0x9000\n"
			"ucode:\n"
			"%s\n"
			"after:\n"
			"poweroff 1\n"
			"h_gp:\n"
			"poweroff 13\n"
			"h_ok:\n"
			"LDW R4, [SP]\n"
			"STW [R3], R4\n"
			"LOAD32 R4, after\n"
			"STW [R3 + 4], R4\n"
			"MFCR R4, CR7\n"
			"STW [R3 + 8], R4\n"
			"poweroff %d\n", okvec[i], ok[i], okvec[i]);
		build(body);
		int rc = asm_run(src, RAM);
		CHECKV(rc == okvec[i], i, rc);
		CHECK(rd32(OBS) == rd32(OBS + 4));
		CHECK(rd32(OBS + 8) == (uint32_t)okvec[i]);
	}
}

static void test_supervisor_int(void) {
	build(
		"setvec 5, h5\n"
		"setvec 13, h_gp\n"
		"INT 5\n"
		"back:\n"
		"poweroff 0\n"
		"h5:\n"
		"LDW R4, [SP]\n"
		"STW [R3], R4\n"
		"LOAD32 R4, back\n"
		"STW [R3 + 4], R4\n"
		"IRET\n"
		"h_gp:\n"
		"poweroff 13\n");
	CHECK(asm_run(src, RAM) == 0);
	CHECK(rd32(OBS) == rd32(OBS + 4));
}

static void test_faults(void) {
	build(
		"setvec 5, h_bus\n"
		"setvec 4, h_ud\n"
		"setvec 0, h_div\n"
		"LOAD32 R1, 0x3000\n"
		"LOAD32 R2, 0x5A5A\n"
		"STW [R1], R2\n"
		"LOAD32 R1, 0x80000000\n"
		"ldi:\n"
		"LDW R2, [R1 + 8]\n"
		"STW [R3], R2\n"
		"bad:\n"
		".byte 0x7B\n"
		"LOAD32 R5, 7\n"
		"LOAD32 R2, 0\n"
		"divi:\n"
		"IDIV R5, R2\n"
		"STW [R3 + 24], R5\n"
		"poweroff 0\n"
		"h_bus:\n"
		"MFCR R4, CR2\n"
		"STW [R3 + 4], R4\n"
		"LDW R4, [SP]\n"
		"STW [R3 + 8], R4\n"
		"LOAD32 R4, ldi\n"
		"STW [R3 + 12], R4\n"
		"LOAD32 R1, 0x2FF8\n"
		"IRET\n"
		"h_ud:\n"
		"LDW R4, [SP]\n"
		"STW [R3 + 16], R4\n"
		"ADD32 R4, 1\n"
		"STW [SP], R4\n"
		"LOAD32 R4, bad\n"
		"STW [R3 + 20], R4\n"
		"IRET\n"
		"h_div:\n"
		"LDW R4, [SP]\n"
		"STW [R3 + 28], R4\n"
		"LOAD32 R4, divi\n"
		"STW [R3 + 32], R4\n"
		"MFCR R4, CR7\n"
		"STW [R3 + 36], R4\n"
		"LOAD32 R2, 2\n"
		"IRET\n");
	CHECK(asm_run(src, RAM) == 0);
	CHECK(rd32(OBS) == 0x5A5A);
	CHECK(rd32(OBS + 4) == 0x80000008);
	CHECK(rd32(OBS + 8) == rd32(OBS + 12));
	CHECK(rd32(OBS + 16) == rd32(OBS + 20));
	CHECK(rd32(OBS + 24) == 3);
	CHECK(rd32(OBS + 28) == rd32(OBS + 32));
	CHECK(rd32(OBS + 36) == 0);
}

static void test_double_triple(void) {
	build(
		".equ EVTDF, 0x100000 - 56\n"
		"LOAD32 R0, EVTDF\n"
		"MTCR CR1, R0\n"
		"LOAD32 R0, h_df\n"
		"LOAD32 R7, EVTDF\n"
		"STW [R7 + 24], R0\n"
		"sc:\n"
		"SYSCALL\n"
		"poweroff 1\n"
		"h_df:\n"
		"LDW R4, [SP]\n"
		"STW [R3], R4\n"
		"LOAD32 R4, sc\n"
		"STW [R3 + 4], R4\n"
		"MFCR R4, CR7\n"
		"STW [R3 + 8], R4\n"
		"MFCR R4, CR2\n"
		"STW [R3 + 12], R4\n"
		"MOVSP R4\n"
		"STW [R3 + 16], R4\n"
		"poweroff 6\n");
	CHECK(asm_run(src, RAM) == 6);
	CHECK(rd32(OBS) == rd32(OBS + 4));
	CHECK(rd32(OBS + 8) == 6);
	CHECK(rd32(OBS + 12) == RAM - 56 + 192);
	CHECK(rd32(OBS + 16) == RAM - 8);
	build(
		"LOAD32 R0, 0x80000000\n"
		"MTCR CR1, R0\n"
		"BRK\n"
		"poweroff 1\n");
	CHECK(asm_run(src, RAM) == EXIT_TRIPLE);
	build(
		"setvec 3, h\n"
		"setvec 6, h\n"
		"LOAD32 R0, 0x90000000\n"
		"SETSP R0\n"
		"BRK\n"
		"poweroff 1\n"
		"h:\n"
		"poweroff 7\n");
	CHECK(asm_run(src, RAM) == EXIT_TRIPLE);
	build(
		"setvec 3, h\n"
		"setvec 6, hdf\n"
		"LOAD32 R0, 0x90000000\n"
		"MTCR CR4, R0\n"
		"touser ucode, 0x9000\n"
		"ucode:\n"
		"BRK\n"
		"poweroff 1\n"
		"h:\n"
		"poweroff 7\n"
		"hdf:\n"
		"poweroff 6\n");
	CHECK(asm_run(src, RAM) == EXIT_TRIPLE);
}

static void test_pic(void) {
	cpu_init();
	pic_raise(3);
	pic_raise(1);
	CHECK(phys_read(PIC_BASE, 4) == 0x0A);
	CHECK(phys_read(PIC_BASE + 0x0C, 4) == 0xFFFFFFFF);
	phys_write(PIC_BASE + 4, 0xFF, 4);
	CHECK(phys_read(PIC_BASE + 4, 4) == 0xFF);
	CHECK(phys_read(PIC_BASE + 0x0C, 4) == 1);
	phys_write(PIC_BASE + 4, 0xFD, 4);
	CHECK(phys_read(PIC_BASE + 0x0C, 4) == 3);
	phys_write(PIC_BASE + 8, 0x08, 4);
	CHECK(phys_read(PIC_BASE, 4) == 0x02);
	CHECK(phys_read(PIC_BASE + 0x0C, 4) == 0xFFFFFFFF);
	phys_write(PIC_BASE + 8, 0xFFFFFFFF, 4);
	CHECK(phys_read(PIC_BASE, 4) == 0);
	static const char *body =
		"CLI\n"
		"setvec 33, h33\n"
		"setvec 35, h35\n"
		"LOAD32 R5, 0\n"
		"LOAD32 R6, PIC\n"
		"LOAD32 R0, MASKVAL\n"
		"STW [R6 + 4], R0\n"
		"IENABLE\n"
		"first:\n"
		"NOP\n"
		"NOP\n"
		"NOP\n"
		"LOAD32 R0, first\n"
		"STW [R3 + 32], R0\n"
		"LDW R0, [R6]\n"
		"STW [R3 + 36], R0\n"
		"poweroff 0\n"
		"h33:\n"
		"LOAD32 R0, 33\n"
		"MOV R1, R3\n"
		"ADD32 R1, R5\n"
		"STW [R1], R0\n"
		"ADD32 R5, 4\n"
		"LDW R0, [SP]\n"
		"STW [R3 + 16], R0\n"
		"LDW R0, [R6 + 12]\n"
		"STW [R3 + 24], R0\n"
		"LOAD32 R0, 2\n"
		"STW [R6 + 8], R0\n"
		"IRET\n"
		"h35:\n"
		"LOAD32 R0, 35\n"
		"MOV R1, R3\n"
		"ADD32 R1, R5\n"
		"STW [R1], R0\n"
		"ADD32 R5, 4\n"
		"LDW R0, [SP]\n"
		"STW [R3 + 20], R0\n"
		"LOAD32 R0, 8\n"
		"STW [R6 + 8], R0\n"
		"IRET\n";
	char b2[8192];
	static const struct { const char *mask; const char *en; uint32_t o0, o1, pend; } cases[] = {
		{"0xFF", "STI", 33, 35, 0},
		{"0x08", "STI", 35, 0, 2},
		{"0x02", "STI", 33, 0, 8},
		{"0x00", "STI", 0, 0, 10},
		{"0xFF", "NOP", 0, 0, 10},
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		snprintf(b2, sizeof(b2), ".equ MASKVAL, %s\n.macro IENABLE\n%s\n.endm\n%s", cases[i].mask, cases[i].en, body);
		build(b2);
		CHECK(asm_load(src, RAM) == 0);
		pic_raise(3);
		pic_raise(1);
		cpu_run();
		CHECK(cpu.exit_code == 0);
		CHECKV(rd32(OBS) == cases[i].o0, i, rd32(OBS));
		CHECKV(rd32(OBS + 4) == cases[i].o1, i, rd32(OBS + 4));
		CHECKV(rd32(OBS + 36) == cases[i].pend, i, rd32(OBS + 36));
		if (cases[i].o0 == 33) {
			CHECK(rd32(OBS + 16) == rd32(OBS + 32));
			CHECK(rd32(OBS + 24) == 1);
		}
	}
}

static void test_timer(void) {
	cpu_init();
	CHECK(phys_read(TIMER_BASE + 0x0C, 4) == 1000);
	phys_write(TIMER_BASE + 0x0C, 10, 4);
	cpu.icount = 105;
	CHECK(phys_read(TIMER_BASE, 4) == 10);
	phys_write(TIMER_BASE + 4, 3, 4);
	phys_write(TIMER_BASE + 8, 1, 4);
	CHECK(phys_read(TIMER_BASE + 4, 4) == 3);
	CHECK(phys_read(TIMER_BASE + 8, 4) == 1);
	cpu.icount = 129;
	dev_events();
	CHECK(!(pic_pending & 1));
	cpu.icount = 130;
	dev_events();
	CHECK(pic_pending & 1);
	CHECK(dev_next == 160);
	pic_pending = 0;
	cpu.icount = 1000;
	dev_events();
	CHECK(pic_pending & 1);
	CHECK(phys_read(TIMER_BASE, 4) == 100);
	phys_write(TIMER_BASE + 0x0C, 0, 4);
	CHECK(phys_read(TIMER_BASE + 0x0C, 4) == 1);
	phys_write(TIMER_BASE + 8, 0, 4);
	pic_pending = 0;
	cpu.icount = 5000;
	dev_events();
	CHECK(!(pic_pending & 1));
	CHECK(phys_read(TIMER_BASE, 4) == 4100);
}

static void test_wfi(void) {
	build("WFI\npoweroff 0\n");
	CHECK(asm_run(src, RAM) == EXIT_DEADLOCK);
	build(
		"setvec 32, h_t\n"
		"LOAD32 R6, PIC\n"
		"LOAD32 R0, 1\n"
		"STW [R6 + 4], R0\n"
		"LOAD32 R6, TIMER\n"
		"LOAD32 R0, 100\n"
		"STW [R6 + 12], R0\n"
		"LOAD32 R0, 50\n"
		"STW [R6 + 4], R0\n"
		"LOAD32 R0, 1\n"
		"STW [R6 + 8], R0\n"
		"LOAD32 R5, 0\n"
		"STI\n"
		"RDCYC R1\n"
		"WFI\n"
		"RDCYC R2\n"
		"WFI\n"
		"RDCYC R7\n"
		"STW [R3], R1\n"
		"STW [R3 + 4], R2\n"
		"STW [R3 + 8], R5\n"
		"STW [R3 + 12], R7\n"
		"poweroff 0\n"
		"h_t:\n"
		"ADD32 R5, 1\n"
		"LOAD32 R0, PIC\n"
		"LOAD32 R4, 1\n"
		"STW [R0 + 8], R4\n"
		"IRET\n");
	CHECK(asm_run(src, RAM) == 0);
	CHECK(rd32(OBS + 8) == 2);
	CHECK(rd32(OBS + 4) - rd32(OBS) > 4000);
	CHECK(rd32(OBS + 4) - rd32(OBS) < 5100);
	CHECK(rd32(OBS + 12) - rd32(OBS + 4) > 4900);
	CHECK(rd32(OBS + 12) - rd32(OBS + 4) < 5100);
	build(
		"LOAD32 R6, PIC\n"
		"LOAD32 R0, 0xFF\n"
		"STW [R6 + 4], R0\n"
		"CLI\n"
		"RDCYC R1\n"
		"WFI\n"
		"RDCYC R2\n"
		"STW [R3], R1\n"
		"STW [R3 + 4], R2\n"
		"poweroff 0\n");
	CHECK(asm_load(src, RAM) == 0);
	pic_raise(2);
	cpu_run();
	CHECK(cpu.exit_code == 0);
	CHECK(rd32(OBS + 4) - rd32(OBS) == 2);
	build(
		"LOAD32 R6, TIMER\n"
		"LOAD32 R0, 1\n"
		"STW [R6 + 4], R0\n"
		"STW [R6 + 8], R0\n"
		"WFI\n"
		"poweroff 0\n");
	CHECK(asm_run(src, RAM) == EXIT_DEADLOCK);
	build(
		"LOAD32 R6, PIC\n"
		"LOAD32 R0, 1\n"
		"STW [R6 + 4], R0\n"
		"LOAD32 R6, TIMER\n"
		"LOAD32 R0, 1000\n"
		"STW [R6 + 4], R0\n"
		"LOAD32 R0, 1\n"
		"STW [R6 + 8], R0\n"
		"WFI\n"
		"poweroff 0\n");
	CHECK(asm_load(src, RAM) == 0);
	cpu.ilimit = 50000;
	dev_next = 0;
	cpu_run();
	CHECK(cpu.exit_code == EXIT_LIMIT);
	CHECK(cpu.icount == 50000);
}

int main(void) {
	test_sup_frame();
	test_user_frame();
	test_privileged();
	test_supervisor_int();
	test_faults();
	test_double_triple();
	test_pic();
	test_timer();
	test_wfi();
	return report("test_p3");
}
