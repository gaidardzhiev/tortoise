/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef TUTIL_H
#define TUTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "dev.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKV(c, a, b) do { checks++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: check failed: %s (0x%08X 0x%08X)\n", __FILE__, __LINE__, #c, (unsigned)(a), (unsigned)(b)); } } while (0)

static inline int asm_build(const char *src, const char *bin) {
	char cmd[512];
	FILE *f = fopen("tests/unit.tmp.asm", "w");
	if (!f) return -1;
	fputs(src, f);
	fclose(f);
	snprintf(cmd, sizeof(cmd), "./as tests/unit.tmp.asm %s > /dev/null", bin);
	return system(cmd);
}

static inline int asm_load(const char *src, uint32_t ram) {
	FILE *f;
	size_t n;
	if (asm_build(src, "tests/unit.tmp.bin")) {
		fprintf(stderr, "assembly failed:\n%s\n", src);
		fails++;
		return -1;
	}
	cpu_init_size(ram);
	f = fopen("tests/unit.tmp.bin", "rb");
	n = fread(cpu.memory, 1, ram, f);
	fclose(f);
	(void)n;
	return 0;
}

static inline int asm_run(const char *src, uint32_t ram) {
	if (asm_load(src, ram)) return -1;
	cpu_run();
	return cpu.exit_code;
}

static inline uint32_t rd32(uint32_t a) {
	return cpu.memory[a] | cpu.memory[a + 1] << 8 | cpu.memory[a + 2] << 16 | (uint32_t)cpu.memory[a + 3] << 24;
}

static inline int report(const char *name) {
	remove("tests/unit.tmp.asm");
	remove("tests/unit.tmp.bin");
	printf("%s: %d checks, %d failures\n", name, checks, fails);
	return fails != 0;
}

#endif
