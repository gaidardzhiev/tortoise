/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: nohup command [args]\n");
		return 127;
	}
	signal(SIGHUP, SIG_IGN);
	if (isatty(1)) {
		int fd = open("nohup.out", O_WRONLY | O_CREAT | O_APPEND, 0600);
		if (fd >= 0) {
			fprintf(stderr, "nohup: appending output to nohup.out\n");
			dup2(fd, 1);
			close(fd);
		}
	}
	if (isatty(2)) dup2(1, 2);
	execvp(argv[1], argv + 1);
	fprintf(stderr, "nohup: %s: not found\n", argv[1]);
	return 127;
}
