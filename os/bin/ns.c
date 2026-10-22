/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
	char path[64], buf[512];
	int fd, n;
	snprintf(path, sizeof(path), "/proc/%s/ns", argc > 1 ? argv[1] : "self");
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "ns: no process %s\n", argc > 1 ? argv[1] : "self");
		return 1;
	}
	while ((n = read(fd, buf, sizeof(buf))) > 0) write(1, buf, n);
	close(fd);
	return 0;
}
