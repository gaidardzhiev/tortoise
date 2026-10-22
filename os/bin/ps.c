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

static char *field(char *buf, const char *name) {
	char *p = strstr(buf, name);
	if (!p) return "";
	p += strlen(name);
	while (*p == '\t' || *p == ' ') p++;
	return p;
}

static void word(char *dst, const char *src, int n) {
	int i;
	for (i = 0; i < n - 1 && src[i] && src[i] != '\n' && src[i] != '\t'; i++) dst[i] = src[i];
	dst[i] = 0;
}

static void cmdline(const char *pid, char *out, int n) {
	char path[64];
	int fd, r, i;
	snprintf(path, sizeof(path), "/proc/%s/cmdline", pid);
	out[0] = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0) return;
	r = read(fd, out, n - 1);
	close(fd);
	if (r < 0) r = 0;
	while (r > 0 && out[r - 1] == 0) r--;
	for (i = 0; i < r; i++)
		if (!out[i]) out[i] = ' ';
	out[r] = 0;
}

int main(int argc, char **argv) {
	int dir = open("/proc", O_RDONLY), me = getpid(), full = 0, i;
	struct dirent d;
	char path[64], buf[512], name[32], cmd[128];
	for (i = 1; i < argc; i++) {
		char *o;
		if (argv[i][0] != '-') continue;
		for (o = argv[i] + 1; *o; o++)
			if (*o == 'f') full = 1;
	}
	if (full) printf("  UID   PID  PPID STATE  CMD\n");
	else printf("  PID  PPID STATE  NAME\n");
	if (dir < 0) {
		fprintf(stderr, "ps: cannot open /proc\n");
		return 1;
	}
	while (getdirent(dir, &d) > 0) {
		int fd, n, pid;
		const char *state;
		char st;
		if (d.d_name[0] < '0' || d.d_name[0] > '9') continue;
		pid = atoi(d.d_name);
		snprintf(path, sizeof(path), "/proc/%s/status", d.d_name);
		fd = open(path, O_RDONLY);
		if (fd < 0) continue;
		n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n <= 0) continue;
		buf[n] = 0;
		word(name, field(buf, "Name:"), sizeof(name));
		st = *field(buf, "State:");
		state = st == 'R' ? (pid == me ? "run" : "ready") : st == 'Z' ? "zombie" : st == 'T' ? "stop" : "sleep";
		if (full) {
			cmdline(d.d_name, cmd, sizeof(cmd));
			printf("%5d %5d %5d %-6s %s\n", atoi(field(buf, "Uid:")), pid, atoi(field(buf, "PPid:")), state, cmd[0] ? cmd : name);
		} else {
			printf("%5d %5d %-6s %s\n", pid, atoi(field(buf, "PPid:")), state, name);
		}
	}
	close(dir);
	return 0;
}
