/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
	char buf[64];
	int fd = open("/tmp/runs", O_RDWR | O_CREAT | O_APPEND, 0666), n, i, runs = 0;
	write(fd, "run\n", 4);
	lseek(fd, 0, SEEK_SET);
	n = read(fd, buf, sizeof(buf));
	close(fd);
	for (i = 0; i < n; i++)
		if (buf[i] == '\n') runs++;
	if (runs < 3) return 0;
	for (;;) pause();
}
