/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include "cpu.h"
#include "opts.h"

int main(int argc, char *argv[]) {
	Opts o;
	if (opts_parse(argc, argv, &o)) {
		opts_usage(argv[0]);
		return 1;
	}
	return emu_main(&o);
}
