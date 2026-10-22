/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	int i = 1, nl = 1;
	if (i < argc && !strcmp(argv[i], "-n")) {
		nl = 0;
		i++;
	}
	for (; i < argc; i++) {
		fputs(argv[i], stdout);
		if (i < argc - 1) putchar(' ');
	}
	if (nl) putchar('\n');
	return 0;
}
