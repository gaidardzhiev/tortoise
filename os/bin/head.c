/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int head(FILE *f, int n) {
	int c, lines = 0;
	while (lines < n && (c = fgetc(f)) != EOF) {
		putchar(c);
		if (c == '\n') lines++;
	}
	return 0;
}

int main(int argc, char **argv) {
	int i = 1, n = 10, rc = 0, many;
	if (i < argc && !strcmp(argv[i], "-n") && i + 1 < argc) {
		n = atoi(argv[i + 1]);
		i += 2;
	} else if (i < argc && argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
		n = atoi(argv[i] + 1);
		i++;
	}
	if (i >= argc) return head(stdin, n);
	many = argc - i > 1;
	for (; i < argc; i++) {
		FILE *f = fopen(argv[i], "r");
		if (!f) {
			fprintf(stderr, "head: cannot open %s\n", argv[i]);
			rc = 1;
			continue;
		}
		if (many) printf("==> %s <==\n", argv[i]);
		head(f, n);
		fclose(f);
	}
	return rc;
}
