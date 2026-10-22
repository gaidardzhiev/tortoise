/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static int show_l, show_w, show_c;

static void count(int fd, const char *name) {
	char buf[512];
	int n, i, inword = 0, l = 0, w = 0, c = 0, first = 1;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (i = 0; i < n; i++) {
			int ch = buf[i];
			c++;
			if (ch == '\n') l++;
			if (ch == ' ' || ch == '\t' || ch == '\n') inword = 0;
			else if (!inword) {
				inword = 1;
				w++;
			}
		}
	}
	if (show_l) printf("%s%d", first ? "" : " ", l), first = 0;
	if (show_w) printf("%s%d", first ? "" : " ", w), first = 0;
	if (show_c) printf("%s%d", first ? "" : " ", c), first = 0;
	if (name) printf(" %s", name);
	printf("\n");
}

int main(int argc, char **argv) {
	int i = 1;
	while (i < argc && argv[i][0] == '-' && argv[i][1]) {
		char *o;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'l') show_l = 1;
			else if (*o == 'w') show_w = 1;
			else if (*o == 'c') show_c = 1;
		}
		i++;
	}
	if (!show_l && !show_w && !show_c) show_l = show_w = show_c = 1;
	if (i >= argc) {
		count(0, NULL);
		return 0;
	}
	for (; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "wc: cannot open %s\n", argv[i]);
			continue;
		}
		count(fd, argv[i]);
		close(fd);
	}
	return 0;
}
