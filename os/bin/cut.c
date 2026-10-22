/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lo[32], hi[32], nr;

static int parse(const char *s) {
	while (*s && nr < 32) {
		int a = 1, b = 100000;
		if (*s >= '0' && *s <= '9') {
			a = atoi(s);
			while (*s >= '0' && *s <= '9') s++;
			b = a;
		}
		if (*s == '-') {
			s++;
			b = 100000;
			if (*s >= '0' && *s <= '9') {
				b = atoi(s);
				while (*s >= '0' && *s <= '9') s++;
			}
		}
		lo[nr] = a;
		hi[nr] = b;
		nr++;
		if (*s == ',') s++;
		else if (*s) return -1;
	}
	return 0;
}

static int wanted(int k) {
	int i;
	for (i = 0; i < nr; i++)
		if (k >= lo[i] && k <= hi[i]) return 1;
	return 0;
}

int main(int argc, char **argv) {
	int i = 1, chars = 0, len;
	char delim = '\t', *l = NULL;
	size_t cap = 0;
	FILE *f = stdin;
	const char *list = NULL;
	while (i < argc && argv[i][0] == '-') {
		if (!strncmp(argv[i], "-d", 2)) delim = argv[i][2] ? argv[i][2] : (i + 1 < argc ? argv[++i][0] : '\t');
		else if (!strncmp(argv[i], "-f", 2)) list = argv[i][2] ? argv[i] + 2 : (i + 1 < argc ? argv[++i] : "");
		else if (!strncmp(argv[i], "-c", 2)) {
			chars = 1;
			list = argv[i][2] ? argv[i] + 2 : (i + 1 < argc ? argv[++i] : "");
		}
		i++;
	}
	if (!list || parse(list) < 0) {
		fprintf(stderr, "usage: cut -d c -f list | -c list [file]\n");
		return 1;
	}
	if (i < argc) {
		f = fopen(argv[i], "r");
		if (!f) {
			fprintf(stderr, "cut: cannot open %s\n", argv[i]);
			return 1;
		}
	}
	while ((len = getline(&l, &cap, f)) >= 0) {
		int k, field = 1, first = 1;
		if (len > 0 && l[len - 1] == '\n') l[--len] = 0;
		if (chars) {
			for (k = 0; k < len; k++)
				if (wanted(k + 1)) putchar(l[k]);
			putchar('\n');
			continue;
		}
		if (!strchr(l, delim)) {
			printf("%s\n", l);
			continue;
		}
		for (k = 0; k <= len; k++) {
			if (k == len || l[k] == delim) {
				field++;
				continue;
			}
			if (wanted(field)) {
				if (!first && (k == 0 || l[k - 1] == delim)) putchar(delim);
				putchar(l[k]);
				first = 0;
			}
		}
		putchar('\n');
	}
	return 0;
}
