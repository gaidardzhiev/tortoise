/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <termios.h>
#include <plan9.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <net/if.h>

#define TIOCGPTN 0x80045430u

static int fails;

static void expect(const char *what, int ok) {
	if (ok) printf("ok   %s\n", what);
	else {
		printf("FAIL %s (errno %d)\n", what, errno);
		fails++;
	}
	fflush(stdout);
}

static int ctl(const char *path, const char *cmd) {
	int fd = open(path, O_WRONLY), r;
	if (fd < 0) return -1;
	r = write(fd, cmd, strlen(cmd));
	close(fd);
	return r == (int)strlen(cmd) ? 0 : -1;
}

static void show_err(const char *what) {
	char buf[ERRMAX];
	buf[0] = 0;
	errstr(buf, sizeof(buf));
	printf("%s: errno %d, errstr %s\n", what, errno, buf);
	fflush(stdout);
}

static int get(const char *path, char *buf, int n) {
	int fd = open(path, O_RDONLY), r;
	buf[0] = 0;
	if (fd < 0) return -1;
	r = read(fd, buf, n - 1);
	close(fd);
	buf[r > 0 ? r : 0] = 0;
	return r;
}

static void listing(const char *dir, char *out) {
	DIR *d = opendir(dir);
	struct dirent *e;
	out[0] = 0;
	if (!d) {
		strcpy(out, "(cannot open)");
		return;
	}
	while ((e = readdir(d)) != NULL) {
		if (out[0]) strcat(out, " ");
		strcat(out, e->d_name);
	}
	closedir(d);
}

static int words(const char *list, const char *w) {
	int n = 0, l = (int)strlen(w);
	const char *p = list;
	while ((p = strstr(p, w)) != NULL) {
		if ((p == list || p[-1] == ' ') && (p[l] == 0 || p[l] == ' ')) n++;
		p += l;
	}
	return n;
}

static unsigned ifflags(int s, const char *name) {
	struct ifreq r;
	memset(&r, 0, sizeof(r));
	strcpy(r.ifr_name, name);
	ioctl(s, SIOCGIFFLAGS, &r);
	return r.ifr_data[0] & 0xFFFF;
}

static unsigned ifget(int s, const char *name, unsigned req) {
	struct ifreq r;
	unsigned char *b = (unsigned char *)r.ifr_data;
	memset(&r, 0, sizeof(r));
	strcpy(r.ifr_name, name);
	ioctl(s, req, &r);
	return ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) | ((unsigned)b[2] << 8) | b[3];
}

static int lflag_is(int fd, unsigned on, unsigned off) {
	struct termios t;
	if (ioctl(fd, TCGETS, &t) < 0) return 0;
	return (t.c_lflag & on) == on && (t.c_lflag & off) == 0;
}

static int cc_is(int fd, int i, int v) {
	struct termios t;
	return ioctl(fd, TCGETS, &t) == 0 && t.c_cc[i] == v;
}

static void tty_cmds(const char *ctlpath, int fd, const char *who) {
	char what[96];
	snprintf(what, sizeof(what), "%s: rawon clears ICANON and ECHO", who);
	expect(what, ctl(ctlpath, "rawon") == 0 && lflag_is(fd, 0, ICANON | ECHO));
	snprintf(what, sizeof(what), "%s: rawoff sets them", who);
	expect(what, ctl(ctlpath, "rawoff") == 0 && lflag_is(fd, ICANON | ECHO, 0));
	snprintf(what, sizeof(what), "%s: echo off", who);
	expect(what, ctl(ctlpath, "echo off") == 0 && lflag_is(fd, 0, ECHO));
	snprintf(what, sizeof(what), "%s: echo on", who);
	expect(what, ctl(ctlpath, "echo on\n") == 0 && lflag_is(fd, ECHO, 0));
	snprintf(what, sizeof(what), "%s: isig off", who);
	expect(what, ctl(ctlpath, "isig off") == 0 && lflag_is(fd, 0, ISIG));
	snprintf(what, sizeof(what), "%s: isig on", who);
	expect(what, ctl(ctlpath, "isig on") == 0 && lflag_is(fd, ISIG, 0));
	snprintf(what, sizeof(what), "%s: vmin 3", who);
	expect(what, ctl(ctlpath, "vmin 3") == 0 && cc_is(fd, VMIN, 3));
	snprintf(what, sizeof(what), "%s: vtime 7", who);
	expect(what, ctl(ctlpath, "vtime 7") == 0 && cc_is(fd, VTIME, 7));
	snprintf(what, sizeof(what), "%s: an unknown command fails", who);
	expect(what, ctl(ctlpath, "bogus") < 0 && errno == EINVAL);
	show_err(who);
	snprintf(what, sizeof(what), "%s: a bad number fails", who);
	expect(what, ctl(ctlpath, "vmin x") < 0 && errno == EINVAL);
	show_err(who);
}

static void devices(void) {
	char a[512], b[512], list[512];
	int fd, fd1, n;
	mkdir("/n", 0755);
	mkdir("/n/c", 0755);
	mkdir("/n/d", 0755);
	mkdir("/n/l", 0755);
	mkdir("/n/i", 0755);
	mkdir("/n/p", 0755);
	expect("rfork RFNAMEG", rfork(RFNAMEG) == 0);
	expect("bind #c", bind9("#c", "/n/c", MREPLACE) == 0);
	listing("/n/c", list);
	listing("/dev", a);
	expect("#c lists like /dev", !strcmp(list, a));
	expect("#c/null reads end of file", get("/n/c/null", a, sizeof(a)) == 0);
	fd = open("#c/zero", O_RDONLY);
	expect("#c/zero gives zeros", fd >= 0 && read(fd, a, 4) == 4 && a[0] == 0 && a[3] == 0);
	close(fd);
	expect("bind -a #p /proc", bind9("#p", "/proc", MAFTER) == 0);
	listing("/proc/1", list);
	printf("/proc/1: %s\n", list);
	expect("a union with itself lists every name once", words(list, "status") == 1);
	expect("#p/1/status reads", get("#p/1/status", a, sizeof(a)) > 0 && !strncmp(a, "Name:", 5));
	expect("bind #D", bind9("#D", "/n/d", MREPLACE) == 0);
	listing("/n/d", list);
	printf("#D: %s\n", list);
	fd = open("/n/d/disk0", O_RDONLY);
	fd1 = open("/dev/disk0", O_RDONLY);
	expect("#D/disk0 is /dev/disk0", fd >= 0 && fd1 >= 0 && read(fd, a, 512) == 512 && read(fd1, b, 512) == 512 && !memcmp(a, b, 512));
	close(fd);
	close(fd1);
	expect("bind #l", bind9("#l", "/n/l", MREPLACE) == 0);
	listing("/n/l/ether0", list);
	printf("#l/ether0: %s\n", list);
	get("/n/l/ether0/ctl", a, sizeof(a));
	printf("#l/ether0/ctl: %s", a);
	expect("bind #I", bind9("#I", "/n/i", MREPLACE) == 0);
	listing("/n/i", list);
	printf("#I: %s\n", list);
	get("/n/i/ipifc/0/ctl", a, sizeof(a));
	printf("#I/ipifc/0/ctl: %s", a);
	get("/n/i/iproute", a, sizeof(a));
	get("/proc/net/route", b, sizeof(b));
	expect("#I/iproute equals /proc/net/route", !strcmp(a, b));
	listing("/net", list);
	printf("/net: %s\n", list);
	expect("bind #|", bind9("#|", "/n/p", MREPLACE) == 0);
	listing("/n/p", list);
	printf("#|: %s\n", list);
	fd = open("/n/p/data", O_RDWR);
	fd1 = open("/n/p/data1", O_RDWR);
	expect("both ends open", fd >= 0 && fd1 >= 0);
	expect("data to data1", write(fd, "ping", 4) == 4 && read(fd1, a, 10) == 4 && !memcmp(a, "ping", 4));
	expect("data1 to data", write(fd1, "pong!", 5) == 5 && read(fd, a, 10) == 5 && !memcmp(a, "pong!", 5));
	expect("fd2path names the end", fd2path(fd1, a, sizeof(a)) == 0 && !strcmp(a, "/n/p/data1"));
	close(fd);
	expect("data1 reads end of file when data is closed", read(fd1, a, 10) == 0);
	signal(SIGPIPE, SIG_IGN);
	expect("writing then fails with EPIPE", write(fd1, "x", 1) == -1 && errno == EPIPE);
	close(fd1);
	unmount(NULL, "/n/p");
	fd = open("#|/data", O_RDWR);
	expect("#|/data opened alone has no other end", fd >= 0 && read(fd, a, 10) == 0);
	close(fd);
	fd = open("#x/foo", O_RDONLY);
	expect("an unknown device fails", fd < 0 && errno == ENOENT);
	show_err("#x");
	errno = ENOTDIR;
	a[0] = 0;
	n = errstr(a, sizeof(a));
	expect("errstr without a kernel message gives strerror", n == 0 && !strcmp(a, strerror(ENOTDIR)));
	strcpy(a, "my own message");
	errstr(a, sizeof(a));
	b[0] = 0;
	errstr(b, sizeof(b));
	expect("errstr swaps the strings", !strcmp(b, "my own message"));
}

static void terminals(void) {
	struct termios save;
	char path[32];
	int m, s, n;
	ioctl(0, TCGETS, &save);
	tty_cmds("/dev/consctl", 0, "consctl");
	ioctl(0, TCSETS, &save);
	m = open("/dev/ptmx", O_RDWR);
	ioctl(m, TIOCGPTN, &n);
	snprintf(path, sizeof(path), "/dev/pts/%d", n);
	s = open(path, O_RDWR);
	snprintf(path, sizeof(path), "/dev/pts/%dctl", n);
	tty_cmds(path, s, "pts ctl");
	close(s);
	close(m);
}

static void network(void) {
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	char a[512];
	expect("ether0 down", ctl("/net/ether0/ctl", "down") == 0 && !(ifflags(s, "eth0") & IFF_UP));
	expect("ether0 up", ctl("/net/ether0/ctl", "up") == 0 && (ifflags(s, "eth0") & IFF_UP));
	expect("promiscuous on", ctl("/net/ether0/ctl", "promiscuous on") == 0 && (ifflags(s, "eth0") & IFF_PROMISC));
	expect("promiscuous off", ctl("/net/ether0/ctl", "promiscuous off") == 0 && !(ifflags(s, "eth0") & IFF_PROMISC));
	expect("ether0: an unknown command fails", ctl("/net/ether0/ctl", "mtu 9000") < 0 && errno == EINVAL);
	show_err("ether0");
	expect("ipifc/1 add", ctl("/net/ipifc/1/ctl", "add 10.0.0.5 255.255.255.0") == 0 && ifget(s, "eth0", SIOCGIFADDR) == 0x0A000005 && ifget(s, "eth0", SIOCGIFNETMASK) == 0xFFFFFF00);
	get("/net/ipifc/1/ctl", a, sizeof(a));
	printf("ipifc/1/ctl: %s", a);
	expect("ipifc/1 down", ctl("/net/ipifc/1/ctl", "down") == 0 && !(ifflags(s, "eth0") & IFF_UP));
	expect("ipifc/1 up", ctl("/net/ipifc/1/ctl", "up") == 0 && (ifflags(s, "eth0") & IFF_UP));
	expect("iproute add", ctl("/net/iproute", "add 192.168.5.0 255.255.255.0 10.0.0.1") == 0);
	get("/net/iproute", a, sizeof(a));
	printf("iproute:\n%s", a);
	expect("iproute remove", ctl("/net/iproute", "remove 192.168.5.0 255.255.255.0") == 0);
	expect("iproute remove of a missing route fails like SIOCDELRT", ctl("/net/iproute", "remove 192.168.5.0 255.255.255.0") < 0 && errno == ESRCH);
	expect("ipifc/1 remove of another address fails", ctl("/net/ipifc/1/ctl", "remove 10.0.0.6 255.255.255.0") < 0 && errno == EINVAL);
	show_err("ipifc");
	expect("ipifc/1 remove", ctl("/net/ipifc/1/ctl", "remove 10.0.0.5 255.255.255.0") == 0 && ifget(s, "eth0", SIOCGIFADDR) == 0);
	expect("ipifc/0 add fails with EPERM like SIOCSIFADDR on lo", ctl("/net/ipifc/0/ctl", "add 10.1.1.1 255.0.0.0") < 0 && errno == EPERM);
	expect("iproute: a bad address fails", ctl("/net/iproute", "add 1.2.3 255.0.0.0 0.0.0.0") < 0 && errno == EINVAL);
	show_err("iproute");
	close(s);
}

static void procs(void) {
	char path[64], a[512];
	int pid, st, p[2], fd;
	pid = fork();
	if (pid == 0) {
		for (;;) pause();
	}
	snprintf(path, sizeof(path), "/proc/%d/note", pid);
	expect("note interrupt is SIGINT", ctl(path, "interrupt") == 0 && waitpid(pid, &st, 0) == pid && WIFSIGNALED(st) && WTERMSIG(st) == SIGINT);
	pid = fork();
	if (pid == 0) {
		for (;;) pause();
	}
	snprintf(path, sizeof(path), "/proc/%d/note", pid);
	expect("note hangup is SIGHUP", ctl(path, "hangup") == 0 && waitpid(pid, &st, 0) == pid && WTERMSIG(st) == SIGHUP);
	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		for (;;) pause();
	}
	setpgid(pid, pid);
	snprintf(path, sizeof(path), "/proc/%d/notepg", pid);
	expect("notepg with another note is SIGTERM to the group", ctl(path, "go away") == 0 && waitpid(pid, &st, 0) == pid && WTERMSIG(st) == SIGTERM);
	pid = fork();
	if (pid == 0) {
		for (;;) pause();
	}
	snprintf(path, sizeof(path), "/proc/%d/ctl", pid);
	expect("ctl nice 5", ctl(path, "nice 5") == 0);
	snprintf(path, sizeof(path), "/proc/%d/status", pid);
	get(path, a, sizeof(a));
	expect("the status shows it", strstr(a, "Nice:\t5") != NULL);
	snprintf(path, sizeof(path), "/proc/%d/ctl", pid);
	expect("ctl: an unknown command fails", ctl(path, "explode") < 0 && errno == EINVAL);
	show_err("ctl");
	expect("ctl kill", ctl(path, "kill") == 0 && waitpid(pid, &st, 0) == pid && WTERMSIG(st) == SIGKILL);
	pipe(p);
	pid = fork();
	if (pid == 0) {
		char c;
		read(p[0], &c, 1);
		_exit(0);
	}
	close(p[0]);
	snprintf(path, sizeof(path), "/proc/%d/fdtab", pid);
	get(path, a, sizeof(a));
	printf("fdtab of the child:\n%s", a);
	snprintf(path, sizeof(path), "/proc/%d/ctl", pid);
	expect("ctl close 0", ctl(path, "close 0") == 0);
	snprintf(path, sizeof(path), "/proc/%d/fdtab", pid);
	get(path, a, sizeof(a));
	expect("descriptor 0 is gone", strncmp(a, "0 ", 2) != 0);
	snprintf(path, sizeof(path), "/proc/%d/ctl", pid);
	expect("ctl close of a closed descriptor fails", ctl(path, "close 0") < 0 && errno == EBADF);
	ctl(path, "kill");
	waitpid(pid, &st, 0);
	close(p[1]);
	pid = fork();
	if (pid == 0) {
		snprintf(path, sizeof(path), "/proc/%d/ctl", getpid());
		ctl(path, "hang");
		execl("/bin/echo", "echo", "after the hang", (char *)0);
		_exit(1);
	}
	expect("ctl hang stops the process at exec", waitpid(pid, &st, WUNTRACED) == pid && WIFSTOPPED(st));
	snprintf(path, sizeof(path), "/proc/%d/args", pid);
	get(path, a, sizeof(a));
	printf("args: %s", a);
	snprintf(path, sizeof(path), "/proc/%d/ctl", pid);
	ctl(path, "start");
	expect("ctl start lets it run", waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
	fd = open("/proc/1/ctl", O_WRONLY);
	expect("ctl of init: kill is ignored by init", fd >= 0 && write(fd, "kill", 4) == 4);
	close(fd);
}

int main(int argc, char **argv) {
	if (argc > 1 && !strcmp(argv[1], "net")) network();
	else {
		devices();
		terminals();
		procs();
	}
	printf("%s\n", fails ? "ctltest: FAILED" : "ctltest: all ok");
	return fails != 0;
}
