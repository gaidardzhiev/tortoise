/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <net9.h>

int main(int argc, char **argv) {
	char adir[NETPATHLEN], ldir[NETPATHLEN];
	int i = 1, verbose = 0, afd;
	if (i < argc && !strcmp(argv[i], "-v")) {
		verbose = 1;
		i++;
	}
	if (argc - i < 2) {
		fprintf(stderr, "usage: listen1 [-v] addr cmd [args...]\n");
		return 1;
	}
	afd = announce(argv[i], adir);
	if (afd < 0) {
		fprintf(stderr, "listen1: cannot announce %s\n", argv[i]);
		return 1;
	}
	signal(SIGCHLD, SIG_IGN);
	for (;;) {
		int lcfd = listen9(adir, ldir), pid;
		if (lcfd < 0) {
			fprintf(stderr, "listen1: listen failed\n");
			return 1;
		}
		if (verbose) fprintf(stderr, "listen1: call on %s\n", ldir);
		pid = fork();
		if (pid == 0) {
			int dfd = accept9(lcfd, ldir);
			if (dfd < 0) _exit(1);
			dup2(dfd, 0);
			dup2(dfd, 1);
			dup2(dfd, 2);
			if (dfd > 2) close(dfd);
			close(lcfd);
			close(afd);
			execv(argv[i + 1], argv + i + 1);
			fprintf(stderr, "listen1: cannot run %s\n", argv[i + 1]);
			_exit(127);
		}
		close(lcfd);
	}
}
