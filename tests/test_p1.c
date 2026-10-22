/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "dev.h"
#include "opts.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint8_t prog[256];
static int plen;

static void b(int v) {
	prog[plen++] = (uint8_t)v;
}

static void d32(uint32_t v) {
	b(v);
	b(v >> 8);
	b(v >> 16);
	b(v >> 24);
}

static void load32(int r, uint32_t v) {
	b(OP_LOAD32);
	b(r);
	d32(v);
}

static void ld32ind(int rd, int rb, int off) {
	b(OP_LOAD32_INDIR);
	b(rd << 4 | rb);
	b(off);
	b(off >> 8);
}

static void st32ind(int rb, int rs, int off) {
	b(OP_STORE32_INDIR);
	b(rb << 4 | rs);
	b(off);
	b(off >> 8);
}

static void run(uint32_t ram) {
	cpu_init_size(ram);
	load_program(prog, plen);
	cpu_run();
}

static int parse(const char *s, Opts *o) {
	char buf[256], *argv[32];
	int argc = 0;
	strcpy(buf, s);
	for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) argv[argc++] = t;
	argv[argc] = NULL;
	return opts_parse(argc, argv, o);
}

static void test_options(void) {
	Opts o;
	CHECK(parse("tortoise p.bin", &o) == 0);
	CHECK(o.ram_mb == 1 && !o.debug && !o.has_limit && strcmp(o.prog, "p.bin") == 0);
	CHECK(parse("tortoise -d p.bin", &o) == 0 && o.debug);
	CHECK(parse("tortoise -m 16 -x 500 -t -i in.txt -D d.img p.bin", &o) == 0);
	CHECK(o.ram_mb == 16 && o.has_limit && o.ilimit == 500 && o.det);
	CHECK(strcmp(o.input, "in.txt") == 0 && strcmp(o.disk, "d.img") == 0);
	CHECK(parse("tortoise -r rom.bin", &o) == 0 && strcmp(o.rom, "rom.bin") == 0 && !o.prog);
	CHECK(parse("tortoise -m 256 p.bin", &o) == 0 && o.ram_mb == 256);
	CHECK(parse("tortoise -m 0 p.bin", &o) != 0);
	CHECK(parse("tortoise -m 257 p.bin", &o) != 0);
	CHECK(parse("tortoise -m abc p.bin", &o) != 0);
	CHECK(parse("tortoise -x p.bin", &o) != 0);
	CHECK(parse("tortoise", &o) != 0);
	CHECK(parse("tortoise -z p.bin", &o) != 0);
	CHECK(parse("tortoise a.bin b.bin", &o) != 0);
	CHECK(parse("tortoise -g 1234 -T t.txt -S s.st -L l.st", &o) == 0);
	CHECK(o.gdb_port == 1234 && strcmp(o.trace, "t.txt") == 0 && strcmp(o.save, "s.st") == 0 && strcmp(o.load, "l.st") == 0);
}

static void test_ramsize(void) {
	cpu_init_size(4u << 20);
	CHECK(cpu.ram_size == 4u << 20);
	CHECK(cpu.SP == 4u << 20);
	CHECK(cpu.cr[CR_RAMSIZE] == 4u << 20);
	plen = 0;
	load32(0, SYS_BASE);
	ld32ind(1, 0, SYS_RAMSIZE);
	ld32ind(2, 0, SYS_VERSION);
	load32(3, 0x003FFFFC);
	st32ind(3, 2, 0);
	ld32ind(4, 3, 0);
	b(OP_HALT);
	run(4u << 20);
	CHECK(cpu.exit_code == 0);
	CHECK(cpu.registers[1] == 4u << 20);
	CHECK(cpu.registers[2] == 2);
	CHECK(cpu.registers[4] == 2);
	run(256u << 20);
	CHECK(cpu.registers[1] == 256u << 20);
	CHECK(cpu.SP == 256u << 20);
}

static void test_bus_errors(void) {
	uint32_t bad[] = {0x00100000, 0x80000000, 0xEFFFFFFC, 0xF0010000, 0xF000A000, 0xFFFF0000};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		plen = 0;
		load32(0, bad[i]);
		ld32ind(1, 0, 0);
		b(OP_HALT);
		run(1u << 20);
		CHECK(cpu.exit_code == EXIT_INVALID);
		CHECK(cpu.inst_pc == 6);
	}
	plen = 0;
	load32(0, 0x000FFFFE);
	ld32ind(1, 0, 0);
	b(OP_HALT);
	run(1u << 20);
	CHECK(cpu.exit_code == EXIT_INVALID);
	plen = 0;
	load32(0, SYS_BASE);
	b(OP_LOAD_INDIR);
	b(0x10);
	b(4);
	b(0);
	b(OP_HALT);
	run(1u << 20);
	CHECK(cpu.exit_code == EXIT_INVALID);
	plen = 0;
	load32(0, SYS_BASE + 2);
	ld32ind(1, 0, 0);
	b(OP_HALT);
	run(1u << 20);
	CHECK(cpu.exit_code == EXIT_INVALID);
	plen = 0;
	load32(0, 0x000FFFFC);
	ld32ind(1, 0, 0);
	b(OP_HALT);
	run(1u << 20);
	CHECK(cpu.exit_code == 0);
	plen = 0;
	b(0xFE);
	run(1u << 20);
	CHECK(cpu.exit_code == EXIT_INVALID);
}

static void test_poweroff(void) {
	uint32_t vals[] = {0, 1, 42, 255, 256 + 7};
	for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		plen = 0;
		load32(0, SYS_BASE);
		load32(1, vals[i]);
		st32ind(0, 1, SYS_POWEROFF);
		load32(2, 0x1234);
		b(OP_HALT);
		run(1u << 20);
		CHECK(cpu.exit_code == (int)(vals[i] & 0xFF));
		CHECK(cpu.registers[2] == 0);
	}
}

static void test_limit(void) {
	plen = 0;
	b(OP_JMP);
	b(0);
	b(0);
	cpu_init_size(1u << 20);
	load_program(prog, plen);
	cpu.ilimit = 1000;
	dev_next = 0;
	cpu_run();
	CHECK(cpu.exit_code == EXIT_LIMIT);
	CHECK(cpu.icount == 1000);
	plen = 0;
	b(OP_NOP);
	b(OP_NOP);
	b(OP_HALT);
	cpu_init_size(1u << 20);
	load_program(prog, plen);
	cpu.ilimit = 3;
	dev_next = 0;
	cpu_run();
	CHECK(cpu.exit_code == 0);
	CHECK(cpu.icount == 3);
}

int main(void) {
	test_options();
	test_ramsize();
	test_bus_errors();
	test_poweroff();
	test_limit();
	printf("test_p1: %d checks, %d failures\n", checks, fails);
	return fails != 0;
}
