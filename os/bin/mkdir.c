/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

static int make_parents(char *path) {
	char *p = path;
	struct stat st;
	while (*p == '/') p++;
	for (;;) {
		char c;
		while (*p && *p != '/') p++;
		c = *p;
		*p = 0;
		if (stat(path, &st) < 0 && mkdir(path, 0777) < 0) {
			*p = c;
			return -1;
		}
		*p = c;
		if (!c) return 0;
		while (*p == '/') p++;
		if (!*p) return 0;
	}
}

int main(int argc, char **argv) {
	int i = 1, rc = 0, parents = 0;
	if (i < argc && !strcmp(argv[i], "-p")) {
		parents = 1;
		i++;
	}
	for (; i < argc; i++) {
		char buf[256];
		int r;
		strncpy(buf, argv[i], sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = 0;
		r = parents ? make_parents(buf) : mkdir(buf, 0777);
		if (r < 0) {
			fprintf(stderr, "mkdir: cannot create %s\n", argv[i]);
			rc = 1;
		}
	}
	return rc;
}
