/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
	int i = 1, mode = 'o', fd = 0, n, off = 0, k;
	unsigned char buf[16];
	if (i < argc && (!strcmp(argv[i], "-x") || !strcmp(argv[i], "-c"))) {
		mode = argv[i][1];
		i++;
	}
	if (i < argc) {
		fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "od: cannot open %s\n", argv[i]);
			return 1;
		}
	}
	for (;;) {
		n = 0;
		while (n < 16) {
			int r = read(fd, buf + n, 16 - n);
			if (r <= 0) break;
			n += r;
		}
		if (n == 0) break;
		printf("%07o", off);
		if (mode == 'c') {
			for (k = 0; k < n; k++) {
				int c = buf[k];
				if (c == '\n') printf("  \\n");
				else if (c == '\t') printf("  \\t");
				else if (c == 0) printf("  \\0");
				else if (c >= 32 && c < 127) printf("   %c", c);
				else printf(" %03o", c);
			}
		} else {
			for (k = 0; k < n; k += 2) {
				unsigned v = buf[k] | (k + 1 < n ? buf[k + 1] << 8 : 0);
				if (mode == 'x') printf(" %04x", v);
				else printf(" %06o", v);
			}
		}
		printf("\n");
		off += n;
		if (n < 16) break;
	}
	printf("%07o\n", off);
	return 0;
}
