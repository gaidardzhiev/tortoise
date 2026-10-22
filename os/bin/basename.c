/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	char buf[512], *s;
	int n;
	if (argc < 2) {
		fprintf(stderr, "usage: basename path [suffix]\n");
		return 1;
	}
	strncpy(buf, argv[1], sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	n = (int)strlen(buf);
	while (n > 1 && buf[n - 1] == '/') buf[--n] = 0;
	s = strrchr(buf, '/');
	if (s && s[1]) s++;
	else if (!s) s = buf;
	if (argc > 2) {
		int l = (int)strlen(s), m = (int)strlen(argv[2]);
		if (l > m && !strcmp(s + l - m, argv[2])) s[l - m] = 0;
	}
	printf("%s\n", s);
	return 0;
}
