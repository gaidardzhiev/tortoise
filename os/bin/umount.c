/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: umount target\n");
		return 1;
	}
	if (umount(argv[1]) < 0) {
		fprintf(stderr, "umount: cannot unmount %s: error %d\n", argv[1], errno);
		return 1;
	}
	return 0;
}
