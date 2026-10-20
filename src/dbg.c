/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "dev.h"

static const char *cr_names[NUM_CR] = {"CTRL", "EVT", "FADDR", "PTBASE", "KSP", "BASE", "LIMIT", "CAUSE", "RAMSIZE"};

static void show_crs(void) {
	for (int i = 0; i < NUM_CR; i++) {
		printf("CR%d %-7s 0x%08X  ", i, cr_names[i], cpu.cr[i]);
		if (i % 3 == 2) printf("\n");
	}
}

static void show_regs(void) {
	printf("PC: 0x%05X  SP: 0x%05X  FLAGS: 0x%02X  MODE: %s\n", cpu.PC, cpu.SP, cpu_get_flags(), cpu.mode == MODE_SUPERVISOR ? "SUPERVISOR" : "USER");
	for (int i = 0; i < 8; i++) {
		printf("R%d: 0x%08X (%u)  ", i, cpu.registers[i], cpu.registers[i]);
		if (i == 3 || i == 7) printf("\n");
	}
	show_crs();
}

void cpu_debug(void) {
	char line[256], cmd[32];
	uint32_t bp = 0xFFFFFFFF;
	printf("tortoise debugger ready. commands: s (step), c (continue), r (regs), cr (control regs), t (count), save <file>, load <file>, b <addr>, d <addr> <n>, x <addr> <n>, q (quit)\n");
	while (!cpu.halted) {
		char dis[64];
		cpu_disassemble(cpu.PC, dis, sizeof(dis));
		printf("[0x%05X] %-24s > ", cpu.PC, dis);
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin)) break;
		cmd[0] = 0;
		sscanf(line, "%31s", cmd);
		char *arg = line;
		while (*arg == ' ' || *arg == '\t') arg++;
		while (*arg && *arg != ' ' && *arg != '\t' && *arg != '\n') arg++;
		if (cmd[0] == 0 || strcmp(cmd, "s") == 0) {
			cpu_step();
		} else if (strcmp(cmd, "r") == 0) {
			show_regs();
		} else if (strcmp(cmd, "b") == 0) {
			unsigned long baddr = 0;
			if (sscanf(arg, "%lx", &baddr) == 1) {
				bp = (uint32_t)baddr;
				printf("breakpoint set at 0x%05X\n", bp);
			}
		} else if (strcmp(cmd, "c") == 0) {
			int first = 1;
			while (!cpu.halted) {
				if (cpu.PC == bp && !first) {
					printf("hit breakpoint at 0x%05X\n", bp);
					break;
				}
				first = 0;
				cpu_step();
			}
		} else if (strcmp(cmd, "d") == 0) {
			unsigned long daddr = cpu.PC;
			int cnt = 8;
			sscanf(arg, "%lx %d", &daddr, &cnt);
			uint32_t cur = (uint32_t)daddr;
			for (int i = 0; i < cnt; i++) {
				char buf[64];
				int sz = cpu_disassemble(cur, buf, sizeof(buf));
				printf("  0x%05X: %s\n", cur, buf);
				cur += sz;
			}
		} else if (strcmp(cmd, "x") == 0) {
			unsigned long xaddr = 0;
			int cnt = 16;
			sscanf(arg, "%lx %d", &xaddr, &cnt);
			for (int i = 0; i < cnt; i++) {
				uint8_t b = 0;
				if (i % 16 == 0) printf("  0x%05lX: ", xaddr + i);
				if (mem_peek((uint32_t)(xaddr + i), &b)) printf("%02X ", b);
				else printf("?? ");
				if (i % 16 == 15 || i == cnt - 1) printf("\n");
			}
		} else if (strcmp(cmd, "cr") == 0) {
			show_crs();
		} else if (strcmp(cmd, "save") == 0 || strcmp(cmd, "load") == 0) {
			char fn[200];
			if (sscanf(arg, "%199s", fn) != 1) printf("usage: %s file\n", cmd);
			else if (cmd[0] == 's') printf(cpu_save(fn) ? "cannot save %s\n" : "saved %s\n", fn);
			else printf(cpu_load(fn) ? "cannot load %s\n" : "loaded %s\n", fn);
		} else if (strcmp(cmd, "t") == 0) {
			printf("instructions executed: %llu\n", (unsigned long long)cpu.icount);
		} else if (strcmp(cmd, "q") == 0) {
			break;
		}
	}
}
