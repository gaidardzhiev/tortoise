/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int icase;

static int eq(int a, int b) {
	if (icase) return tolower(a) == tolower(b);
	return a == b;
}

static const char *class_end(const char *p) {
	p++;
	if (*p == '^') p++;
	if (*p == ']') p++;
	while (*p && *p != ']') p++;
	return *p ? p + 1 : NULL;
}

static int one(const char *p, int c) {
	if (*p == '.') return c != 0;
	if (*p == '[') {
		int neg = 0, hit = 0;
		p++;
		if (*p == '^') {
			neg = 1;
			p++;
		}
		if (*p == ']') {
			hit = c == ']';
			p++;
		}
		while (*p && *p != ']') {
			if (p[1] == '-' && p[2] && p[2] != ']') {
				if ((c >= p[0] && c <= p[2]) || (icase && tolower(c) >= tolower(p[0]) && tolower(c) <= tolower(p[2]))) hit = 1;
				p += 3;
			} else {
				if (eq(c, *p)) hit = 1;
				p++;
			}
		}
		return c && hit != neg;
	}
	if (*p == '\\' && p[1]) return eq(c, p[1]);
	return c && eq(c, *p);
}

static const char *atom_end(const char *p) {
	if (*p == '[') return class_end(p);
	if (*p == '\\' && p[1]) return p + 2;
	return p + 1;
}

static int match_here(const char *p, const char *s) {
	const char *e;
	if (!*p) return 1;
	if (p[0] == '$' && !p[1]) return !*s;
	e = atom_end(p);
	if (!e) return 0;
	if (*e == '*') {
		const char *t = s;
		do {
			if (match_here(e + 1, t)) return 1;
		} while (*t && one(p, (unsigned char)*t++));
		return 0;
	}
	if (one(p, (unsigned char)*s)) return match_here(e, s + 1);
	return 0;
}

static int match(const char *p, const char *s) {
	if (*p == '^') return match_here(p + 1, s);
	do {
		if (match_here(p, s)) return 1;
	} while (*s++);
	return 0;
}

int main(int argc, char **argv) {
	int i = 1, invert = 0, count = 0, number = 0, any = 0, many;
	const char *pat;
	while (i < argc && argv[i][0] == '-' && argv[i][1]) {
		char *o;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'v') invert = 1;
			else if (*o == 'i') icase = 1;
			else if (*o == 'c') count = 1;
			else if (*o == 'n') number = 1;
		}
		i++;
	}
	if (i >= argc) {
		fprintf(stderr, "usage: grep [-vicn] pattern [file ...]\n");
		return 2;
	}
	pat = argv[i++];
	many = argc - i > 1;
	do {
		FILE *f = stdin;
		const char *name = i < argc ? argv[i] : "-";
		char *l = NULL;
		size_t cap = 0;
		int ln = 0, n = 0, len;
		if (i < argc) {
			f = fopen(argv[i], "r");
			if (!f) {
				fprintf(stderr, "grep: cannot open %s\n", argv[i]);
				i++;
				continue;
			}
		}
		while ((len = getline(&l, &cap, f)) >= 0) {
			ln++;
			if (len > 0 && l[len - 1] == '\n') l[len - 1] = 0;
			if (match(pat, l) != invert) {
				n++;
				any = 1;
				if (!count) {
					if (many) printf("%s:", name);
					if (number) printf("%d:", ln);
					printf("%s\n", l);
				}
			}
		}
		if (count) {
			if (many) printf("%s:", name);
			printf("%d\n", n);
		}
		free(l);
		if (f != stdin) fclose(f);
		i++;
	} while (i < argc);
	return any ? 0 : 1;
}
