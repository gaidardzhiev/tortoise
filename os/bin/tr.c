/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

static int expand(const char *s, char *out) {
	int n = 0;
	while (*s && n < 255) {
		int c = (unsigned char)*s;
		if (c == '\\' && s[1]) {
			s++;
			c = *s == 'n' ? '\n' : *s == 't' ? '\t' : (unsigned char)*s;
		}
		if (s[1] == '-' && s[2]) {
			int e = (unsigned char)s[2], k;
			for (k = c; k <= e && n < 255; k++) out[n++] = (char)k;
			s += 3;
			continue;
		}
		out[n++] = (char)c;
		s++;
	}
	return n;
}

int main(int argc, char **argv) {
	char a[256], b[256], map[256], del[256];
	int i = 1, na, nb = 0, c, k, deleting = 0;
	if (i < argc && !strcmp(argv[i], "-d")) {
		deleting = 1;
		i++;
	}
	if (i >= argc || (!deleting && i + 1 >= argc)) {
		fprintf(stderr, "usage: tr [-d] set1 [set2]\n");
		return 1;
	}
	na = expand(argv[i], a);
	if (!deleting) nb = expand(argv[i + 1], b);
	for (k = 0; k < 256; k++) {
		map[k] = (char)k;
		del[k] = 0;
	}
	for (k = 0; k < na; k++) {
		if (deleting) del[(unsigned char)a[k]] = 1;
		else map[(unsigned char)a[k]] = b[k < nb ? k : nb - 1];
	}
	while ((c = getchar()) != EOF) {
		if (del[c]) continue;
		putchar(map[c]);
	}
	return 0;
}
