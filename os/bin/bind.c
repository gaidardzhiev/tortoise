/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <plan9.h>

int main(int argc, char **argv) {
	int i = 1, flags = MREPLACE;
	while (i < argc && argv[i][0] == '-' && argv[i][1]) {
		char *o;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'b') flags = (flags & ~3) | MBEFORE;
			else if (*o == 'a') flags = (flags & ~3) | MAFTER;
			else if (*o == 'c') flags |= MCREATE;
			else {
				fprintf(stderr, "usage: bind [-b|-a|-c] new old\n");
				return 1;
			}
		}
		i++;
	}
	if (argc - i != 2) {
		fprintf(stderr, "usage: bind [-b|-a|-c] new old\n");
		return 1;
	}
	if (bind9(argv[i], argv[i + 1], flags) < 0) {
		fprintf(stderr, "bind: %s %s: %s\n", argv[i], argv[i + 1], strerror(errno));
		return 1;
	}
	return 0;
}
