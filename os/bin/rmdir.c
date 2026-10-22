/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
	int i, rc = 0;
	for (i = 1; i < argc; i++)
		if (rmdir(argv[i]) < 0) {
			fprintf(stderr, "rmdir: cannot remove %s\n", argv[i]);
			rc = 1;
		}
	return rc;
}
