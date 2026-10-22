/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	int i = 1;
	char **e;
	if (i < argc && !strcmp(argv[i], "-i")) {
		static char *empty[1];
		environ = empty;
		i++;
	}
	for (; i < argc && strchr(argv[i], '='); i++) putenv(argv[i]);
	if (i < argc) {
		execvp(argv[i], argv + i);
		fprintf(stderr, "env: %s: not found\n", argv[i]);
		return 127;
	}
	for (e = environ; e && *e; e++) printf("%s\n", *e);
	return 0;
}
