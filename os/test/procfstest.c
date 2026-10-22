/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int fails;
static char buf[4096];

static void expect(const char *what, int got, int want) {
	if (got == want) printf("ok   %s\n", what);
	else {
		printf("FAIL %s: got %d want %d (errno %d)\n", what, got, want, errno);
		fails++;
	}
	fflush(stdout);
}

static int slurp(const char *path) {
	int fd = open(path, O_RDONLY), n = 0, r;
	if (fd < 0) return -1;
	while ((r = read(fd, buf + n, 64)) > 0 && n < (int)sizeof(buf) - 65) n += r;
	close(fd);
	buf[n] = 0;
	return n;
}

static char *field(const char *name) {
	static char v[64];
	char *p = strstr(buf, name);
	int i = 0;
	v[0] = 0;
	if (!p) return v;
	p += strlen(name);
	if (*p == '\t') p++;
	while (*p && *p != '\n' && i < 63) v[i++] = *p++;
	v[i] = 0;
	return v;
}

static void show(const char *path, int masked) {
	char *s, *e;
	int n = slurp(path);
	printf("--- %s\n", path);
	if (n < 0) {
		printf("cannot read, errno %d\n", errno);
		return;
	}
	for (s = buf; *s; s = e + 1) {
		e = strchr(s, '\n');
		if (!e) e = s + strlen(s);
		if (masked && (!strncmp(s, "Pid:", 4) || !strncmp(s, "PPid:", 5) || !strncmp(s, "Pgid:", 5) || !strncmp(s, "Sid:", 4) || !strncmp(s, "VmSize:", 7) || !strncmp(s, "VmRSS:", 6) || !strncmp(s, "Utime:", 6) || !strncmp(s, "Stime:", 6))) {
			char *t = strchr(s, '\t');
			printf("%.*s\t(varies)\n", (int)(t ? t - s : e - s), s);
		} else if (masked == 2 && (strstr(s, " text") || strstr(s, " data") || strstr(s, " heap"))) {
			printf("(varies) %.*s\n", (int)(e - s - 18), s + 18);
		} else {
			printf("%.*s\n", (int)(e - s), s);
		}
		if (!*e) break;
	}
}

static void show_zero(const char *path) {
	int n = slurp(path), i;
	printf("--- %s\n", path);
	for (i = 0; i < n; i++) putchar(buf[i] ? buf[i] : '|');
	printf("\n");
}

static void link_of(const char *path) {
	char t[128];
	int n = readlink(path, t, sizeof(t) - 1);
	t[n > 0 ? n : 0] = 0;
	if (!strncmp(t, "pipe:[", 6)) strcpy(t, "pipe:[id]");
	printf("%s -> %s\n", path, n < 0 ? "(error)" : t);
}

int main(void) {
	char path[64], c, *argv[4], *envp[3];
	int p[2], pid, st, i, n, fd;
	struct stat sb;
	pipe(p);
	pid = fork();
	if (pid == 0) {
		dup2(p[1], 1);
		close(p[0]);
		close(p[1]);
		setpgid(0, 0);
		argv[0] = "procchild";
		argv[1] = "alpha";
		argv[2] = "two words";
		argv[3] = NULL;
		envp[0] = "A=1";
		envp[1] = "B=two";
		envp[2] = NULL;
		execve("/test/procchild", argv, envp);
		_exit(127);
	}
	close(p[1]);
	read(p[0], &c, 1);
	close(p[0]);
	snprintf(path, sizeof(path), "/proc/%d/status", pid);
	for (i = 0; i < 100; i++) {
		slurp(path);
		if (field("State:")[0] == 'S') break;
		usleep(5 * 10000);
	}
	show(path, 1);
	slurp(path);
	expect("Pid", atoi(field("Pid:")), pid);
	expect("PPid", atoi(field("PPid:")), getpid());
	expect("Pgid is its own", atoi(field("Pgid:")), pid);
	expect("Sid is ours", atoi(field("Sid:")), getsid(0));
	expect("VmSize counts the regions", atoi(field("VmSize:")) > 1024, 1);
	expect("VmRSS is smaller", atoi(field("VmRSS:")) < atoi(field("VmSize:")) && atoi(field("VmRSS:")) > 0, 1);
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	show_zero(path);
	snprintf(path, sizeof(path), "/proc/%d/environ", pid);
	show_zero(path);
	expect("environ is mode 0400", stat(path, &sb) == 0 && (sb.st_mode & 0777) == 0400, 1);
	snprintf(path, sizeof(path), "/proc/%d/maps", pid);
	show(path, 2);
	snprintf(path, sizeof(path), "/proc/%d/cwd", pid);
	link_of(path);
	snprintf(path, sizeof(path), "/proc/%d/exe", pid);
	link_of(path);
	snprintf(path, sizeof(path), "/proc/%d/fd", pid);
	fd = open(path, O_RDONLY);
	{
		struct dirent d;
		while (getdirent(fd, &d) > 0) {
			char lp[64];
			if (d.d_name[0] == '.') continue;
			snprintf(lp, sizeof(lp), "%s/%s", path, d.d_name);
			link_of(lp);
		}
	}
	close(fd);
	snprintf(path, sizeof(path), "/proc/%d/fd/9", pid);
	expect("a closed descriptor is missing", open(path, O_RDONLY) == -1 && errno == ENOENT, 1);
	snprintf(path, sizeof(path), "/proc/%d/status", pid);
	kill(pid, SIGSTOP);
	waitpid(pid, &st, WUNTRACED);
	slurp(path);
	expect("stopped is T", field("State:")[0], 'T');
	kill(pid, SIGCONT);
	kill(pid, SIGKILL);
	for (i = 0; i < 100; i++) {
		slurp(path);
		if (field("State:")[0] == 'Z') break;
		usleep(5 * 10000);
	}
	expect("dead and not waited for is Z", field("State:")[0], 'Z');
	waitpid(pid, &st, 0);
	expect("after wait it is gone", open(path, O_RDONLY) == -1 && errno == ENOENT, 1);
	snprintf(path, sizeof(path), "/proc/%d/status", getpid());
	slurp(path);
	expect("running is R", field("State:")[0], 'R');
	n = readlink("/proc/self", path, sizeof(path) - 1);
	path[n > 0 ? n : 0] = 0;
	expect("/proc/self names us", atoi(path), getpid());
	show("/proc/mounts", 0);
	show("/proc/cpuinfo", 0);
	slurp("/proc/version");
	expect("version", strncmp(buf, "TORTOISE 3.0 ", 13) == 0 && strlen(buf) == 24 && buf[17] == '-', 1);
	slurp("/proc/meminfo");
	expect("meminfo has four lines", strstr(buf, "MemTotal: ") == buf && strstr(buf, "\nMemFree: ") && strstr(buf, "\nKernelPool: ") && strstr(buf, "\nKernelPoolFree: ") != NULL, 1);
	expect("free memory is less than total", atoi(field("MemFree:")) < atoi(field("MemTotal:")), 1);
	slurp("/proc/uptime");
	{
		unsigned t = atoi(buf);
		char want[32];
		snprintf(want, sizeof(want), "%u %u.%02u\n", t, t / 100, t % 100);
		expect("uptime is ticks and seconds", strcmp(buf, want), 0);
	}
	slurp("/proc/loadavg");
	{
		char *s = strchr(buf, ' ');
		int run = atoi(buf), total = s ? atoi(s + 1) : 0, last = s && strchr(s + 1, ' ') ? atoi(strchr(s + 1, ' ') + 1) : 0, count = 0;
		struct dirent d;
		fd = open("/proc", O_RDONLY);
		while (getdirent(fd, &d) > 0)
			if (d.d_name[0] >= '0' && d.d_name[0] <= '9') count++;
		close(fd);
		expect("loadavg: at least we run", run >= 1, 1);
		expect("loadavg: total matches /proc", total, count);
		expect("loadavg: last pid", last >= pid, 1);
	}
	expect("missing pid", open("/proc/30000/status", O_RDONLY) == -1 && errno == ENOENT, 1);
	fd = open("/proc/uptime", O_WRONLY);
	expect("/proc files cannot be written", fd < 0 || write(fd, "x", 1) < 0, 1);
	if (fd >= 0) close(fd);
	printf("%s\n", fails ? "procfstest: FAILED" : "procfstest: all ok");
	return fails != 0;
}
