/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>

int main(int argc, char **argv) {
	int i, rc = 0;
	for (i = 1; i < argc; i++) {
		if (utime(argv[i], NULL) == 0) continue;
		{
			int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
			if (fd < 0) {
				fprintf(stderr, "touch: cannot touch %s\n", argv[i]);
				rc = 1;
			} else {
				close(fd);
			}
		}
	}
	return rc;
}
