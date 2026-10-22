/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
	int i, rc = 0;
	char **e;
	if (argc < 2) {
		for (e = environ; e && *e; e++) printf("%s\n", *e);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		char *v = getenv(argv[i]);
		if (v) printf("%s\n", v);
		else rc = 1;
	}
	return rc;
}
