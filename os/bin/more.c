/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

static int page(FILE *f, int tty) {
	int c, lines = 0;
	while ((c = fgetc(f)) != EOF) {
		putchar(c);
		if (c == '\n' && ++lines >= 24 && tty >= 0) {
			struct termios o, t;
			char k = 'q';
			fputs("--More--", stdout);
			fflush(stdout);
			tcgetattr(tty, &o);
			t = o;
			t.c_lflag &= ~(ICANON | ECHO);
			t.c_cc[VMIN] = 1;
			t.c_cc[VTIME] = 0;
			tcsetattr(tty, TCSANOW, &t);
			if (read(tty, &k, 1) != 1) k = 'q';
			tcsetattr(tty, TCSANOW, &o);
			fputs("\r        \r", stdout);
			if (k == 'q') return 1;
			lines = k == '\n' || k == '\r' ? 23 : 0;
		}
	}
	fflush(stdout);
	return 0;
}

int main(int argc, char **argv) {
	int i, tty = isatty(1) ? open("/dev/tty", O_RDONLY) : -1;
	if (argc < 2) return page(stdin, tty) < 0;
	for (i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "r");
		if (!f) {
			fprintf(stderr, "more: cannot open %s\n", argv[i]);
			continue;
		}
		if (page(f, tty)) break;
		fclose(f);
	}
	return 0;
}
