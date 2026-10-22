/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

static int all;

static void field(const char *dir, const char *name, char *out, int n) {
	char path[64];
	FILE *f;
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	out[0] = 0;
	f = fopen(path, "r");
	if (!f) return;
	if (fgets(out, n, f)) out[strcspn(out, "\n")] = 0;
	fclose(f);
}

static void net9(const char *proto) {
	char base[32], dir[64], l[40], r[40], st[40];
	DIR *d;
	struct dirent *e;
	snprintf(base, sizeof(base), "/net/%s", proto);
	d = opendir(base);
	if (!d) return;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		snprintf(dir, sizeof(dir), "%s/%s", base, e->d_name);
		field(dir, "local", l, sizeof(l));
		field(dir, "remote", r, sizeof(r));
		field(dir, "status", st, sizeof(st));
		if (!all && (!strcmp(st, "Listen") || !strcmp(st, "Closed"))) continue;
		printf("%-5s %-4s %-21s %-21s %s\n", proto, e->d_name, l, r, st);
	}
	closedir(d);
}

static void inet(const char *proto, const char *path) {
	FILE *f = fopen(path, "r");
	char line[160], l[32], r[32], st[16];
	unsigned rq, sq;
	int pid;
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%31s %31s %15s %u %u %d", l, r, st, &rq, &sq, &pid) != 6) continue;
		if (!all && (!strcmp(st, "LISTEN") || !strcmp(st, "UNCONNECTED") || !strcmp(st, "CLOSED"))) continue;
		printf("%-5s %6u %6u %-21s %-21s %s\n", proto, rq, sq, l, r, st);
	}
	fclose(f);
}

static void unixs(void) {
	FILE *f = fopen("/proc/net/unix", "r");
	char line[160], p[110], t[16], st[16];
	if (!f) return;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%109s %15s %15s", p, t, st) != 3) continue;
		if (!all && strcmp(st, "CONNECTED")) continue;
		printf("%-5s %-6s %-12s %s\n", "unix", t, st, p);
	}
	fclose(f);
}

int main(int argc, char **argv) {
	int i, t = 0, u = 0, x = 0, n = 0;
	for (i = 1; i < argc; i++) {
		char *o;
		if (argv[i][0] != '-') continue;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 't') t = 1;
			else if (*o == 'u') u = 1;
			else if (*o == 'x') x = 1;
			else if (*o == 'a') all = 1;
			else if (*o == 'n') n = 1;
		}
	}
	if (!t && !u && !x) t = u = x = 1;
	if (n) {
		printf("%-5s %-4s %-21s %-21s %s\n", "Proto", "Conv", "Local", "Remote", "State");
		if (t) net9("tcp");
		if (u) net9("udp");
		return 0;
	}
	if (t || u) printf("%-5s %6s %6s %-21s %-21s %s\n", "Proto", "Recv-Q", "Send-Q", "Local Address", "Foreign Address", "State");
	if (t) inet("tcp", "/proc/net/tcp");
	if (u) inet("udp", "/proc/net/udp");
	if (x) {
		printf("%-5s %-6s %-12s %s\n", "Proto", "Type", "State", "Path");
		unixs();
	}
	return 0;
}
