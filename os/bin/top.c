/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <dirent.h>
#include <termios.h>

static int slurp(const char *path, char *buf, int n) {
	int fd = open(path, O_RDONLY), r;
	if (fd < 0) return -1;
	r = read(fd, buf, n - 1);
	close(fd);
	buf[r > 0 ? r : 0] = 0;
	return r;
}

static char *field(char *buf, const char *name) {
	char *p = strstr(buf, name);
	if (!p) return "";
	p += strlen(name);
	while (*p == '\t' || *p == ' ') p++;
	return p;
}

static void word(char *d, const char *s, int n) {
	int i;
	for (i = 0; i < n - 1 && s[i] && s[i] != '\n' && s[i] != '\t'; i++) d[i] = s[i];
	d[i] = 0;
}

static void screen(void) {
	char buf[512], up[64], load[64], path[64], name[32], state[4];
	DIR *d = opendir("/proc");
	struct dirent *e;
	slurp("/proc/uptime", up, sizeof(up));
	slurp("/proc/loadavg", load, sizeof(load));
	slurp("/proc/meminfo", buf, sizeof(buf));
	printf("\033[H\033[Jtop: uptime %s", strchr(up, ' ') ? strchr(up, ' ') + 1 : up);
	printf("load %s", load);
	printf("memory free %s kB\n\n", strtok(field(buf, "MemFree:"), " "));
	printf("  PID  PPID S  NI  UTIME  STIME NAME\n");
	if (!d) return;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		snprintf(path, sizeof(path), "/proc/%s/status", e->d_name);
		if (slurp(path, buf, sizeof(buf)) <= 0) continue;
		word(name, field(buf, "Name:"), sizeof(name));
		word(state, field(buf, "State:"), sizeof(state));
		printf("%5d %5d %s %3d %6d %6d %s\n", atoi(e->d_name), atoi(field(buf, "PPid:")), state, atoi(field(buf, "Nice:")), atoi(field(buf, "Utime:")), atoi(field(buf, "Stime:")), name);
	}
	closedir(d);
	fflush(stdout);
}

int main(void) {
	struct termios o, t;
	int tty = tcgetattr(0, &o) == 0;
	if (tty) {
		t = o;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
	}
	for (;;) {
		struct pollfd p;
		char c;
		screen();
		p.fd = 0;
		p.events = POLLIN;
		if (poll(&p, 1, 100) > 0) {
			if (read(0, &c, 1) <= 0 || c == 'q') break;
		}
	}
	if (tty) tcsetattr(0, TCSANOW, &o);
	printf("\n");
	return 0;
}
