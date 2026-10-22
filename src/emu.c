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

static uint32_t le32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int fits(uint32_t a, uint32_t n) {
	return (uint64_t)a + n <= cpu.ram_size;
}

static int load_tex(FILE *f, const char *path) {
	uint8_t h[32];
	uint32_t entry, ta, ts, da, dsz, bs, ba;
	if (fread(h, 1, 32, f) != 32) goto bad;
	entry = le32(h + 4);
	ta = le32(h + 8);
	ts = le32(h + 12);
	da = le32(h + 16);
	dsz = le32(h + 20);
	bs = le32(h + 24);
	ba = (uint32_t)(((uint64_t)da + dsz + 3) & ~3ull);
	if (!fits(ta, ts) || !fits(da, dsz) || !fits(ba, bs)) {
		fprintf(stderr, "%s: TEX1 image does not fit in RAM\n", path);
		return -1;
	}
	if (fread(cpu.memory + ta, 1, ts, f) != ts) goto bad;
	if (fread(cpu.memory + da, 1, dsz, f) != dsz) goto bad;
	memset(cpu.memory + ba, 0, bs);
	cpu.PC = entry;
	if (cpu_verbose) printf("loaded TEX1 image, entry 0x%08X\n", entry);
	return 0;
bad:
	fprintf(stderr, "%s: truncated TEX1 image\n", path);
	return -1;
}

int load_image(const char *path) {
	uint8_t magic[4];
	FILE *f = fopen(path, "rb");
	if (!f) {
		perror("loading binary");
		return -1;
	}
	if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "TEX1", 4) == 0) {
		int r;
		rewind(f);
		r = load_tex(f, path);
		fclose(f);
		return r;
	}
	rewind(f);
	size_t read = fread(cpu.memory, 1, cpu.ram_size, f);
	for (size_t i = read; i < cpu.ram_size; i++) {
		cpu.memory[i] = 0xFF;
	}
	if (cpu_verbose) printf("loaded %zu bytes into memory\n", read);
	fclose(f);
	return 0;
}

int emu_main(Opts *o) {
	FILE *trace = NULL;
	int rc = 0;
	cpu_verbose = o->verbose;
	cpu_init_size(o->ram_mb << 20);
	if (o->disk && disk_open(o->disk)) {
		fprintf(stderr, "cannot open disk image %s\n", o->disk);
		return EXIT_FATAL;
	}
	if (o->disk2 && disk_open_unit(1, o->disk2)) {
		fprintf(stderr, "cannot open disk image %s\n", o->disk2);
		return EXIT_FATAL;
	}
	if ((o->net || o->mac || o->pcap) && net_config(o->net, o->mac, o->pcap)) return EXIT_FATAL;
	if (o->input) {
		if (uart_set_input(o->input)) {
			fprintf(stderr, "cannot open input file %s\n", o->input);
			return EXIT_FATAL;
		}
	} else if (!o->debug) {
		uart_use_stdin(1);
	}
	dev_set_det(o->det);
	dev_open_display();
	if (o->rom) {
		if (rom_load(o->rom)) {
			fprintf(stderr, "cannot load rom %s\n", o->rom);
			return EXIT_FATAL;
		}
		cpu.PC = ROM_BASE;
	}
	if (o->prog && load_image(o->prog)) return EXIT_FATAL;
	if (o->rom) cpu.PC = ROM_BASE;
	if (o->load && cpu_load(o->load)) {
		fprintf(stderr, "cannot load state %s\n", o->load);
		return EXIT_FATAL;
	}
	if (o->trace) {
		trace = fopen(o->trace, "w");
		if (!trace) {
			fprintf(stderr, "cannot create trace file %s\n", o->trace);
			return EXIT_FATAL;
		}
		cpu_set_trace(trace);
	}
	if (o->has_limit) cpu.ilimit = o->ilimit;
	cpu_set_dcache(!o->nocache);
	dev_next = 0;
	if (o->gdb_port) rc = gdb_serve(o->gdb_port);
	else if (o->debug) cpu_debug();
	else cpu_run();
	dev_flush();
	cpu_trace_end();
	if (trace) fclose(trace);
	if (rc) return rc;
	if (o->save && cpu_save(o->save)) {
		fprintf(stderr, "cannot save state to %s\n", o->save);
		return EXIT_FATAL;
	}
	dev_term_restore();
	if (o->count) fprintf(stderr, "instructions: %llu\n", (unsigned long long)cpu.icount);
	return cpu.exit_code;
}
