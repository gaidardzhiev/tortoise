/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int apply(const char *spec, unsigned mode, int isdir, unsigned *out) {
	const char *s = spec;
	if (*s >= '0' && *s <= '7') {
		unsigned m = 0;
		for (; *s; s++) {
			if (*s < '0' || *s > '7') return -1;
			m = m * 8 + (unsigned)(*s - '0');
		}
		*out = m & 07777;
		return 0;
	}
	while (*s) {
		unsigned who = 0, bits = 0;
		char op;
		for (; *s == 'u' || *s == 'g' || *s == 'o' || *s == 'a'; s++)
			who |= *s == 'u' ? 04700 : *s == 'g' ? 02070 : *s == 'o' ? 01007 : 07777;
		if (!who) who = 07777 & ~umask(umask(0));
		if (*s != '+' && *s != '-' && *s != '=') return -1;
		op = *s++;
		for (; *s && *s != ','; s++) {
			if (*s == 'r') bits |= 0444;
			else if (*s == 'w') bits |= 0222;
			else if (*s == 'x') bits |= 0111;
			else if (*s == 'X') bits |= (isdir || (mode & 0111)) ? 0111 : 0;
			else if (*s == 's') bits |= 06000;
			else if (*s == 't') bits |= 01000;
			else return -1;
		}
		bits &= who;
		if (op == '+') mode |= bits;
		else if (op == '-') mode &= ~bits;
		else mode = (mode & ~(who & 07777)) | bits;
		if (*s == ',') s++;
	}
	*out = mode & 07777;
	return 0;
}

int main(int argc, char **argv) {
	int i, rc = 0;
	if (argc < 3) {
		fprintf(stderr, "usage: chmod mode file ...\n");
		return 1;
	}
	for (i = 2; i < argc; i++) {
		struct stat st;
		unsigned m;
		if (stat(argv[i], &st) < 0) {
			fprintf(stderr, "chmod: %s: no such file\n", argv[i]);
			rc = 1;
			continue;
		}
		if (apply(argv[1], st.st_mode & 07777, S_ISDIR(st.st_mode), &m) < 0) {
			fprintf(stderr, "chmod: bad mode %s\n", argv[1]);
			return 1;
		}
		if (chmod(argv[i], m) < 0) {
			fprintf(stderr, "chmod: %s: permission denied\n", argv[i]);
			rc = 1;
		}
	}
	return rc;
}
