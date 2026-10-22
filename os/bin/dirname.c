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
		fprintf(stderr, "usage: dirname path\n");
		return 1;
	}
	strncpy(buf, argv[1], sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	n = (int)strlen(buf);
	while (n > 1 && buf[n - 1] == '/') buf[--n] = 0;
	s = strrchr(buf, '/');
	if (!s) {
		printf(".\n");
		return 0;
	}
	while (s > buf && *s == '/') s--;
	if (s == buf && *s == '/') s[1] = 0;
	else s[1] = 0;
	printf("%s\n", buf);
	return 0;
}
