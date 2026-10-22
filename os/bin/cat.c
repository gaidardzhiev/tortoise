/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int number, line = 1, at_start = 1;

static int copy(int fd) {
	char buf[512];
	int n, i;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		if (!number) {
			if (write(1, buf, n) != n) return 1;
			continue;
		}
		for (i = 0; i < n; i++) {
			if (at_start) {
				printf("%6d\t", line++);
				at_start = 0;
			}
			putchar(buf[i]);
			if (buf[i] == '\n') at_start = 1;
		}
	}
	fflush(stdout);
	return n < 0;
}

int main(int argc, char **argv) {
	int i = 1, rc = 0;
	if (i < argc && !strcmp(argv[i], "-n")) {
		number = 1;
		i++;
	}
	if (i >= argc) return copy(0);
	for (; i < argc; i++) {
		int fd = strcmp(argv[i], "-") ? open(argv[i], O_RDONLY) : 0;
		if (fd < 0) {
			fprintf(stderr, "cat: cannot open %s\n", argv[i]);
			rc = 1;
			continue;
		}
		rc |= copy(fd);
		if (fd) close(fd);
	}
	return rc;
}
