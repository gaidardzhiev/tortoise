/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int slurp(const char *path, char *buf, int n) {
	int fd = open(path, O_RDONLY), r;
	if (fd < 0) return -1;
	r = read(fd, buf, n - 1);
	close(fd);
	buf[r > 0 ? r : 0] = 0;
	return r;
}

int main(void) {
	char up[64], load[64], *p;
	unsigned secs, run, total;
	if (slurp("/proc/uptime", up, sizeof(up)) < 0 || slurp("/proc/loadavg", load, sizeof(load)) < 0) {
		fprintf(stderr, "uptime: cannot read /proc\n");
		return 1;
	}
	p = strchr(up, ' ');
	secs = p ? (unsigned)atoi(p + 1) : 0;
	run = (unsigned)atoi(load);
	p = strchr(load, ' ');
	total = p ? (unsigned)atoi(p + 1) : 0;
	printf("up %u:%02u:%02u, %u processes, %u running\n", secs / 3600, secs / 60 % 60, secs % 60, total, run);
	return 0;
}
