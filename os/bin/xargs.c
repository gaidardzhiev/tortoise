/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int run(char **argv) {
	int pid = fork(), st;
	if (pid == 0) {
		execvp(argv[0], argv);
		fprintf(stderr, "xargs: %s: not found\n", argv[0]);
		_exit(127);
	}
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 125;
}

int main(int argc, char **argv) {
	char *args[70], word[256];
	int i = 1, max = 60, base, n, c, len, rc = 0;
	if (i + 1 < argc && !strcmp(argv[i], "-n")) {
		max = atoi(argv[i + 1]);
		if (max < 1 || max > 60) max = 60;
		i += 2;
	}
	base = 0;
	if (i >= argc) args[base++] = "echo";
	for (; i < argc && base < 8; i++) args[base++] = argv[i];
	n = base;
	len = 0;
	for (;;) {
		c = getchar();
		if (c == EOF || c == ' ' || c == '\t' || c == '\n') {
			if (len) {
				word[len] = 0;
				args[n++] = strdup(word);
				len = 0;
				if (n - base >= max) {
					args[n] = NULL;
					if (run(args)) rc = 123;
					n = base;
				}
			}
			if (c == EOF) break;
			continue;
		}
		if (len < 255) word[len++] = (char)c;
	}
	if (n > base) {
		args[n] = NULL;
		if (run(args)) rc = 123;
	}
	return rc;
}
