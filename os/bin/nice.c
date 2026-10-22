/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	int i = 1, inc = 10;
	if (i + 1 < argc && !strcmp(argv[i], "-n")) {
		inc = atoi(argv[i + 1]);
		i += 2;
	}
	if (i >= argc) {
		printf("%d\n", nice(0));
		return 0;
	}
	if (nice(inc) == -1 && inc < 0) fprintf(stderr, "nice: cannot raise the priority\n");
	execvp(argv[i], argv + i);
	fprintf(stderr, "nice: %s: not found\n", argv[i]);
	return 127;
}
