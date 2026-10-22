/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	int i = 1, counts = 0, n = 0, len;
	char *l = NULL, *prev = NULL;
	size_t cap = 0;
	FILE *f = stdin;
	if (i < argc && !strcmp(argv[i], "-c")) {
		counts = 1;
		i++;
	}
	if (i < argc) {
		f = fopen(argv[i], "r");
		if (!f) {
			fprintf(stderr, "uniq: cannot open %s\n", argv[i]);
			return 1;
		}
	}
	while ((len = getline(&l, &cap, f)) >= 0) {
		if (len > 0 && l[len - 1] == '\n') l[len - 1] = 0;
		if (prev && !strcmp(prev, l)) {
			n++;
			continue;
		}
		if (prev) {
			if (counts) printf("%7d %s\n", n, prev);
			else printf("%s\n", prev);
			free(prev);
		}
		prev = strdup(l);
		n = 1;
	}
	if (prev) {
		if (counts) printf("%7d %s\n", n, prev);
		else printf("%s\n", prev);
	}
	return 0;
}
