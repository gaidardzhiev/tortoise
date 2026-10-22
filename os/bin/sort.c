/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reverse, numeric;

static int cmp(const void *a, const void *b) {
	const char *x = *(char *const *)a, *y = *(char *const *)b;
	int r;
	if (numeric) {
		int p = atoi(x), q = atoi(y);
		r = p < q ? -1 : p > q;
		if (!r) r = strcmp(x, y);
	} else {
		r = strcmp(x, y);
	}
	return reverse ? -r : r;
}

static char **v;
static int n, cap;

static void load(FILE *f) {
	char *l = NULL;
	size_t c = 0;
	int len;
	while ((len = getline(&l, &c, f)) >= 0) {
		if (len > 0 && l[len - 1] == '\n') l[len - 1] = 0;
		if (n == cap) {
			char **nv = malloc((unsigned)(cap * 2 + 64) * sizeof(char *));
			if (n) memcpy(nv, v, (unsigned)n * sizeof(char *));
			free(v);
			v = nv;
			cap = cap * 2 + 64;
		}
		v[n++] = strdup(l);
	}
	free(l);
}

int main(int argc, char **argv) {
	int i = 1, unique = 0;
	while (i < argc && argv[i][0] == '-' && argv[i][1]) {
		char *o;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'r') reverse = 1;
			else if (*o == 'n') numeric = 1;
			else if (*o == 'u') unique = 1;
		}
		i++;
	}
	if (i >= argc) load(stdin);
	for (; i < argc; i++) {
		FILE *f = fopen(argv[i], "r");
		if (!f) {
			fprintf(stderr, "sort: cannot open %s\n", argv[i]);
			return 2;
		}
		load(f);
		fclose(f);
	}
	qsort(v, (unsigned)n, sizeof(char *), cmp);
	for (i = 0; i < n; i++) {
		if (unique && i > 0 && cmp(&v[i], &v[i - 1]) == 0) continue;
		printf("%s\n", v[i]);
	}
	return 0;
}
