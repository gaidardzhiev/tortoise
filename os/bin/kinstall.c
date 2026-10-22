/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAXK (1023 * 512)

int main(int argc, char **argv) {
	struct stat st;
	char *buf;
	int fd, d, n, sectors;
	if (argc != 2) {
		fprintf(stderr, "usage: kinstall kernel.tex\n");
		return 1;
	}
	if (stat(argv[1], &st) < 0 || !S_ISREG(st.st_mode)) {
		fprintf(stderr, "kinstall: cannot stat %s\n", argv[1]);
		return 1;
	}
	if (st.st_size < 32 || st.st_size > MAXK) {
		fprintf(stderr, "kinstall: %s is %u bytes; a kernel must fit in sectors 1 to 1023\n", argv[1], st.st_size);
		return 1;
	}
	sectors = (int)((st.st_size + 511) / 512);
	buf = calloc((unsigned)sectors, 512);
	fd = open(argv[1], O_RDONLY);
	if (!buf || fd < 0 || read(fd, buf, st.st_size) != (int)st.st_size) {
		fprintf(stderr, "kinstall: cannot read %s\n", argv[1]);
		return 1;
	}
	close(fd);
	if (memcmp(buf, "TEX1", 4)) {
		fprintf(stderr, "kinstall: %s is not a TEX1 executable\n", argv[1]);
		return 1;
	}
	d = open("/dev/disk0", O_WRONLY);
	if (d < 0) {
		fprintf(stderr, "kinstall: cannot open /dev/disk0\n");
		return 1;
	}
	if (lseek(d, 512, SEEK_SET) != 512 || (n = write(d, buf, (unsigned)sectors * 512)) != sectors * 512) {
		fprintf(stderr, "kinstall: write to /dev/disk0 failed\n");
		return 1;
	}
	close(d);
	sync();
	printf("kinstall: %u bytes written to sectors 1 to %d\n", st.st_size, sectors);
	return 0;
}
