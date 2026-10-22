/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

static void name_of(const char *pid, char *out, int n) {
	char path[64], buf[256], *p;
	int fd, r;
	snprintf(path, sizeof(path), "/proc/%s/status", pid);
	strcpy(out, "?");
	fd = open(path, O_RDONLY);
	if (fd < 0) return;
	r = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	buf[r > 0 ? r : 0] = 0;
	p = strstr(buf, "Name:\t");
	if (!p) return;
	p += 6;
	r = 0;
	while (*p && *p != '\n' && r < n - 1) out[r++] = *p++;
	out[r] = 0;
}

int main(void) {
	DIR *d = opendir("/proc"), *fdd;
	struct dirent *e, *f;
	char path[96], link[256], name[32];
	if (!d) {
		fprintf(stderr, "lsof: cannot open /proc\n");
		return 1;
	}
	printf("%-10s %5s %4s %s\n", "COMMAND", "PID", "FD", "NAME");
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		name_of(e->d_name, name, sizeof(name));
		snprintf(path, sizeof(path), "/proc/%s/fd", e->d_name);
		fdd = opendir(path);
		if (!fdd) continue;
		while ((f = readdir(fdd)) != NULL) {
			int n;
			if (f->d_name[0] == '.') continue;
			snprintf(path, sizeof(path), "/proc/%s/fd/%s", e->d_name, f->d_name);
			n = readlink(path, link, sizeof(link) - 1);
			link[n > 0 ? n : 0] = 0;
			printf("%-10s %5s %4s %s\n", name, e->d_name, f->d_name, link);
		}
		closedir(fdd);
	}
	closedir(d);
	return 0;
}
