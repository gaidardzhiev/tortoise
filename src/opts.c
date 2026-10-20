/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "opts.h"

void opts_usage(const char *argv0) {
	fprintf(stderr, "usage: %s [-d] [-v] [-n] [-t] [-m MB] [-x N] [-r rom.bin] [-D disk.img] [-E disk2.img] [-i file] [-g port] [-T file] [-S file] [-L file] [program.bin]\n", argv0);
}

static int num(const char *s, uint64_t *v) {
	char *e;
	if (!s || !*s) return -1;
	unsigned long long x = strtoull(s, &e, 0);
	if (*e) return -1;
	*v = x;
	return 0;
}

int opts_parse(int argc, char **argv, Opts *o) {
	uint64_t v;
	memset(o, 0, sizeof(*o));
	o->ram_mb = 1;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] != '-' || a[1] == 0) {
			if (o->prog) return -1;
			o->prog = a;
			continue;
		}
		if (a[2]) return -1;
		switch (a[1]) {
		case 'd': o->debug = 1; continue;
		case 'v': o->verbose = 1; continue;
		case 'n': o->nocache = 1; continue;
		case 't': o->det = 1; continue;
		case 'c': o->count = 1; continue;
		}
		if (i + 1 >= argc) return -1;
		const char *p = argv[++i];
		switch (a[1]) {
		case 'm':
			if (num(p, &v) || v < 1 || v > MAX_RAM_MB) return -1;
			o->ram_mb = (uint32_t)v;
			break;
		case 'x':
			if (num(p, &v)) return -1;
			o->has_limit = 1;
			o->ilimit = v;
			break;
		case 'g':
			if (num(p, &v) || v < 1 || v > 65535) return -1;
			o->gdb_port = (int)v;
			break;
		case 'r': o->rom = p; break;
		case 'D': o->disk = p; break;
		case 'E': o->disk2 = p; break;
		case 'i': o->input = p; break;
		case 'T': o->trace = p; break;
		case 'S': o->save = p; break;
		case 'L': o->load = p; break;
		default: return -1;
		}
	}
	if (!o->prog && !o->rom && !o->load) return -1;
	return 0;
}
