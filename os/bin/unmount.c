/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <plan9.h>

int main(int argc, char **argv) {
	int r;
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: unmount [new] old\n");
		return 1;
	}
	r = argc == 2 ? unmount(NULL, argv[1]) : unmount(argv[1], argv[2]);
	if (r < 0) {
		fprintf(stderr, "unmount: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}
