/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef OPTS_H
#define OPTS_H

#include <stdint.h>

typedef struct {
	int debug;
	int verbose;
	int nocache;
	int det;
	int count;
	uint32_t ram_mb;
	int has_limit;
	uint64_t ilimit;
	int gdb_port;
	const char *rom;
	const char *disk;
	const char *disk2;
	const char *net;
	const char *mac;
	const char *pcap;
	const char *input;
	const char *trace;
	const char *save;
	const char *load;
	const char *prog;
} Opts;

int opts_parse(int argc, char **argv, Opts *o);
void opts_usage(const char *argv0);
int emu_main(Opts *o);
int load_image(const char *path);
int gdb_serve(int port);

#endif
