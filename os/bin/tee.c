/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
	int fds[16], n = 0, i = 1, append = 0, r, k, rc = 0;
	char buf[512];
	if (i < argc && !strcmp(argv[i], "-a")) {
		append = 1;
		i++;
	}
	for (; i < argc && n < 16; i++) {
		int fd = open(argv[i], O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0666);
		if (fd < 0) {
			fprintf(stderr, "tee: cannot open %s\n", argv[i]);
			rc = 1;
			continue;
		}
		fds[n++] = fd;
	}
	while ((r = read(0, buf, sizeof(buf))) > 0) {
		write(1, buf, r);
		for (k = 0; k < n; k++) write(fds[k], buf, r);
	}
	for (k = 0; k < n; k++) close(fds[k]);
	return rc;
}
