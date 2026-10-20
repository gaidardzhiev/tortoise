#include <time.h>
#include <sys/wait.h>
#include "tutil.h"

#define U(o) (UART_BASE + (o))
#define D(o) (DISK_BASE + (o))

static void test_uart_file(void) {
	FILE *f = fopen("tests/p5in.tmp", "wb");
	fputs("ab\n", f);
	fclose(f);
	cpu_init();
	CHECK(uart_set_input("tests/p5in.tmp") == 0);
	CHECK(phys_read(U(4), 4) == 3);
	CHECK(pic_pending == 0);
	phys_write(U(8), 1, 4);
	CHECK(phys_read(U(8), 4) == 1);
	CHECK(pic_pending & 2);
	pic_pending = 0;
	CHECK(phys_read(U(0), 4) == 'a');
	CHECK(pic_pending & 2);
	pic_pending = 0;
	CHECK(phys_read(U(4), 4) == 3);
	uint32_t v = 0;
	CHECK(legacy_in(IO_CONSOLE_DATA, &v) && v == 'b');
	pic_pending = 0;
	CHECK(phys_read(U(0), 4) == '\n');
	CHECK(!(pic_pending & 2));
	CHECK(phys_read(U(4), 4) == 2);
	CHECK(phys_read(U(0), 4) == 0);
	CHECK(legacy_in(IO_CONSOLE_DATA, &v) && v == 'A');
	phys_write(U(8), 0, 4);
	phys_write(U(0), '\n', 4);
	CHECK(!(pic_pending & 4));
	phys_write(U(8), 2, 4);
	phys_write(U(0), '\n', 4);
	CHECK(pic_pending & 4);
	fflush(stdout);
	uart_set_input("/dev/null");
	remove("tests/p5in.tmp");
}

static void test_disk_dma(void) {
	cpu_init();
	CHECK(phys_read(D(0x14), 4) == 256);
	for (int i = 0; i < 1024; i++) cpu.memory[0x10000 + i] = (uint8_t)(i * 7 + 3);
	phys_write(D(0x00), 10, 4);
	phys_write(D(0x04), 2, 4);
	phys_write(D(0x08), 0x10000, 4);
	CHECK(phys_read(D(0x00), 4) == 10 && phys_read(D(0x04), 4) == 2 && phys_read(D(0x08), 4) == 0x10000);
	cpu.icount = 500;
	phys_write(D(0x0C), 2, 4);
	CHECK(phys_read(D(0x10), 4) == 1);
	cpu.icount = 2499;
	dev_events();
	CHECK(phys_read(D(0x10), 4) == 1);
	CHECK(!(pic_pending & 8));
	cpu.icount = 2500;
	dev_events();
	CHECK(phys_read(D(0x10), 4) == 4);
	CHECK(pic_pending & 8);
	phys_write(D(0x10), 0, 4);
	CHECK(phys_read(D(0x10), 4) == 0);
	pic_pending = 0;
	phys_write(D(0x08), 0x20000, 4);
	phys_write(D(0x0C), 1, 4);
	phys_write(D(0x0C), 2, 4);
	cpu.icount = 4500;
	dev_events();
	CHECK(phys_read(D(0x10), 4) == 4);
	CHECK(memcmp(cpu.memory + 0x10000, cpu.memory + 0x20000, 1024) == 0);
	CHECK(cpu.memory[0x20000 + 1024] == 0);
	phys_write(D(0x0C), 3, 4);
	CHECK(phys_read(D(0x10), 4) == 4);
	static const struct { uint32_t sec, cnt, addr; } bad[] = {
		{256, 1, 0x1000}, {255, 2, 0x1000}, {0, 1, 0x000FFF00}, {0, 1, 0xFFFFFE00}, {0xFFFFFFFF, 2, 0},
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		pic_pending = 0;
		phys_write(D(0x10), 0, 4);
		phys_write(D(0x00), bad[i].sec, 4);
		phys_write(D(0x04), bad[i].cnt, 4);
		phys_write(D(0x08), bad[i].addr, 4);
		phys_write(D(0x0C), 1, 4);
		cpu.icount += 1000 * (uint64_t)bad[i].cnt;
		dev_events();
		CHECKV(phys_read(D(0x10), 4) == 2, i, phys_read(D(0x10), 4));
		CHECK(pic_pending & 8);
	}
	phys_write(D(0x10), 0xFF, 4);
	CHECK(phys_read(D(0x10), 4) == 0);
	phys_write(D(0x14), 5, 4);
	CHECK(phys_read(D(0x14), 4) == 256);
	cpu.memory[0x5000] = 0x77;
	cpu.disk_sector = 9;
	cpu.disk_buffer = 0x5000;
	legacy_out(IO_DISK_CMD, 2);
	phys_write(D(0x00), 9, 4);
	phys_write(D(0x04), 1, 4);
	phys_write(D(0x08), 0x6000, 4);
	phys_write(D(0x0C), 1, 4);
	cpu.icount += 1000;
	dev_events();
	CHECK(cpu.memory[0x6000] == 0x77);
}

static int sh(const char *cmd) {
	int r = system(cmd);
	return WIFEXITED(r) ? WEXITSTATUS(r) : -1;
}

static void test_persistence(void) {
	FILE *f = fopen("tests/p5disk.tmp", "wb");
	static uint8_t z[512 * 16];
	fwrite(z, 1, sizeof(z), f);
	fclose(f);
	CHECK(asm_build(
		"LOAD32 R7, 0xF0003000\n"
		"LOAD32 R1, 0x20000\n"
		"LOAD32 R2, 0xDEADBEEF\n"
		"STW [R1], R2\n"
		"LOAD32 R2, 0x600DCAFE\n"
		"STW [R1 + 508], R2\n"
		"LOAD32 R0, 13\n"
		"STW [R7], R0\n"
		"LOAD32 R0, 1\n"
		"STW [R7 + 4], R0\n"
		"STW [R7 + 8], R1\n"
		"LOAD32 R0, 2\n"
		"STW [R7 + 12], R0\n"
		"w:\n"
		"LDW R0, [R7 + 16]\n"
		"TESTI R0, 1\n"
		"BNE w\n"
		"LOAD32 R1, 0xF0007000\n"
		"AND32 R0, 6\n"
		"STW [R1], R0\n", "tests/p5w.tmp") == 0);
	CHECK(asm_build(
		"LOAD32 R7, 0xF0003000\n"
		"LDW R0, [R7 + 20]\n"
		"CMP32 R0, 16\n"
		"BNE bad\n"
		"LOAD32 R1, 0x30000\n"
		"LOAD32 R0, 13\n"
		"STW [R7], R0\n"
		"LOAD32 R0, 1\n"
		"STW [R7 + 4], R0\n"
		"STW [R7 + 8], R1\n"
		"STW [R7 + 12], R0\n"
		"w:\n"
		"LDW R0, [R7 + 16]\n"
		"TESTI R0, 1\n"
		"BNE w\n"
		"LDW R2, [R1]\n"
		"CMP32 R2, 0xDEADBEEF\n"
		"BNE bad\n"
		"LDW R2, [R1 + 508]\n"
		"CMP32 R2, 0x600DCAFE\n"
		"BNE bad\n"
		"LOAD32 R1, 0xF0007000\n"
		"LOAD32 R0, 0\n"
		"STW [R1], R0\n"
		"bad:\n"
		"LOAD32 R1, 0xF0007000\n"
		"LOAD32 R0, 9\n"
		"STW [R1], R0\n", "tests/p5r.tmp") == 0);
	CHECK(sh("./tortoise -D tests/p5disk.tmp tests/p5w.tmp < /dev/null") == 4);
	CHECK(sh("./tortoise -D tests/p5disk.tmp tests/p5r.tmp < /dev/null") == 0);
	f = fopen("tests/p5disk.tmp", "rb");
	fseek(f, 13 * 512, SEEK_SET);
	uint8_t b[4] = {0};
	CHECK(fread(b, 1, 4, f) == 4);
	fclose(f);
	CHECK(b[0] == 0xEF && b[1] == 0xBE && b[2] == 0xAD && b[3] == 0xDE);
	CHECK(sh("./tortoise tests/p5r.tmp < /dev/null") == 9);
	CHECK(sh("./tortoise -D tests/missing.tmp tests/p5r.tmp < /dev/null 2> /dev/null") == 1);
	remove("tests/p5disk.tmp");
	remove("tests/p5w.tmp");
	remove("tests/p5r.tmp");
}

static void test_rtc_display(void) {
	cpu_init();
	dev_set_det(1);
	CHECK(phys_read(RTC_BASE, 4) == 0);
	cpu.icount = 25000000;
	CHECK(phys_read(RTC_BASE, 4) == 2);
	dev_set_det(0);
	uint32_t t = phys_read(RTC_BASE, 4);
	uint32_t now = (uint32_t)time(NULL);
	CHECK(t + 2 >= now && t <= now + 2);
	CHECK(phys_read(FB_BASE, 4) == 0);
	CHECK(phys_read(FB_BASE + 4, 4) == 0);
	phys_write(FB_BASE + 8, 0x1234, 4);
	CHECK(phys_read(FB_BASE + 8, 4) == 0);
	CHECK(phys_read(KBD_BASE + 4, 4) == 0);
	CHECK(phys_read(KBD_BASE, 4) == 0);
}

int main(void) {
	test_uart_file();
	test_disk_dma();
	test_persistence();
	test_rtc_display();
	return report("test_p5");
}
