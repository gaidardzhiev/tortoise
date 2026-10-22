/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static char *lines[1000];

static void tail(FILE *f, int n) {
	char *l = NULL;
	size_t cap = 0;
	int count = 0, i, first;
	if (n > 1000) n = 1000;
	while (getline(&l, &cap, f) >= 0) {
		free(lines[count % n]);
		lines[count % n] = strdup(l);
		count++;
	}
	free(l);
	first = count > n ? count - n : 0;
	for (i = first; i < count; i++) {
		fputs(lines[i % n], stdout);
		free(lines[i % n]);
		lines[i % n] = NULL;
	}
	fflush(stdout);
}

int main(int argc, char **argv) {
	int i = 1, n = 10, follow = 0;
	FILE *f = stdin;
	while (i < argc && argv[i][0] == '-' && argv[i][1]) {
		if (!strcmp(argv[i], "-n") && i + 1 < argc) n = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-f")) follow = 1;
		else if (argv[i][1] >= '0' && argv[i][1] <= '9') n = atoi(argv[i] + 1);
		i++;
	}
	if (n <= 0) n = 1;
	if (i < argc) {
		f = fopen(argv[i], "r");
		if (!f) {
			fprintf(stderr, "tail: cannot open %s\n", argv[i]);
			return 1;
		}
	}
	tail(f, n);
	if (follow) {
		char buf[512];
		int fd = fileno(f), r;
		for (;;) {
			r = read(fd, buf, sizeof(buf));
			if (r > 0) write(1, buf, r);
			else usleep(100000);
		}
	}
	return 0;
}
