/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <plan9.h>

static int is_srv(const char *s) {
	return !strncmp(s, "/srv/", 5) || !strncmp(s, "#s/", 3);
}

static int mount_srv(int argc, char **argv) {
	int i = 1, flags = MREPLACE, fd;
	char e[ERRMAX];
	while (i < argc && argv[i][0] == '-' && strspn(argv[i] + 1, "bac") == strlen(argv[i] + 1) && argv[i][1]) {
		char *o;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'b') flags = (flags & ~3) | MBEFORE;
			else if (*o == 'a') flags = (flags & ~3) | MAFTER;
			else flags |= MCREATE;
		}
		i++;
	}
	if ((argc - i != 2 && argc - i != 3) || !is_srv(argv[i])) {
		fprintf(stderr, "usage: mount [-b|-a|-c] /srv/name old [aname]\n");
		return 1;
	}
	fd = open(argv[i], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "mount: cannot open %s\n", argv[i]);
		return 1;
	}
	if (mount9(fd, -1, argv[i + 1], flags, argc - i == 3 ? argv[i + 2] : "") < 0) {
		e[0] = 0;
		errstr(e, sizeof(e));
		fprintf(stderr, "mount: %s on %s: %s\n", argv[i], argv[i + 1], e);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv) {
	int k;
	for (k = 1; k < argc && argv[k][0] == '-' && strcmp(argv[k], "-t"); k++) ;
	if (k < argc && is_srv(argv[k])) return mount_srv(argc, argv);
	if (argc == 1) {
		char buf[512];
		int fd = open("/proc/mounts", O_RDONLY), n;
		if (fd < 0) {
			fprintf(stderr, "usage: mount source target type\n");
			return 1;
		}
		while ((n = read(fd, buf, sizeof(buf))) > 0) write(1, buf, n);
		close(fd);
		return 0;
	}
	if (argc == 5 && !strcmp(argv[1], "-t")) {
		if (mount(argv[3], argv[4], argv[2]) < 0) {
			fprintf(stderr, "mount: cannot mount %s on %s: error %d\n", argv[3], argv[4], errno);
			return 1;
		}
		return 0;
	}
	if (argc != 4) {
		fprintf(stderr, "usage: mount source target type | mount -t type source target\n");
		return 1;
	}
	if (mount(argv[1], argv[2], argv[3]) < 0) {
		fprintf(stderr, "mount: cannot mount %s on %s: error %d\n", argv[1], argv[2], errno);
		return 1;
	}
	return 0;
}
