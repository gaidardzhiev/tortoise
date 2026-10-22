/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	if (argc == 3 && !strcmp(argv[1], "-t")) {
		usleep((unsigned)atoi(argv[2]) * 10000);
		return 0;
	}
	if (argc != 2) {
		fprintf(stderr, "usage: sleep seconds | sleep -t ticks\n");
		return 1;
	}
	sleep((unsigned)atoi(argv[1]));
	return 0;
}
