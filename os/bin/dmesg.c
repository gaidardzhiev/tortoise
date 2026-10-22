/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
	char buf[512];
	int fd = open("/dev/kmsg", O_RDONLY), n;
	if (fd < 0) {
		fprintf(stderr, "dmesg: cannot open /dev/kmsg\n");
		return 1;
	}
	while ((n = read(fd, buf, sizeof(buf))) > 0) write(1, buf, n);
	close(fd);
	return 0;
}
