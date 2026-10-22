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
#include <plan9.h>
#include <net9.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int fails;

static void expect(const char *what, int ok) {
	if (ok) printf("ok   %s\n", what);
	else {
		printf("FAIL %s (errno %d)\n", what, errno);
		fails++;
	}
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

static void field(const char *dir, const char *name, char *out, int n) {
	char path[64];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	get(path, out, n);
	out[strcspn(out, "\n")] = 0;
}

static const char *cs(const char *q, char *ans, int n) {
	int fd = open("/net/cs", O_RDWR), r;
	ans[0] = 0;
	if (fd < 0) return "no /net/cs";
	if (write(fd, q, strlen(q)) < 0) {
		ans[0] = 0;
		errstr(ans, n);
		close(fd);
		return ans;
	}
	lseek(fd, 0, SEEK_SET);
	r = read(fd, ans, n - 1);
	ans[r > 0 ? r : 0] = 0;
	ans[strcspn(ans, "\n")] = 0;
	close(fd);
	return ans;
}

static void listing(const char *dir, char *out) {
	DIR *d = opendir(dir);
	struct dirent *e;
	out[0] = 0;
	while (d && (e = readdir(d)) != NULL) {
		if (out[0]) strcat(out, " ");
		strcat(out, e->d_name);
	}
	if (d) closedir(d);
}

static void ether(void) {
	unsigned char frame[64], got[128];
	char path[64], num[8];
	int cfd, dfd, n;
	cfd = open("/net/ether0/clone", O_RDWR);
	n = cfd >= 0 ? read(cfd, num, sizeof(num) - 1) : -1;
	expect("ether0/clone gives a conversation", n > 0);
	num[n > 0 ? n : 0] = 0;
	expect("connect to a frame type", write(cfd, "connect 34997", 13) == 13);
	snprintf(path, sizeof(path), "/net/ether0/%s/data", num);
	dfd = open(path, O_RDWR);
	memset(frame, 0, sizeof(frame));
	memset(frame, 0xFF, 6);
	frame[12] = 0x88;
	frame[13] = 0xB5;
	memcpy(frame + 14, "raw frame", 9);
	expect("write a raw frame", dfd >= 0 && write(dfd, frame, 60) == 60);
	n = read(dfd, got, sizeof(got));
	expect("the loop gives it back with its type", n >= 23 && got[12] == 0x88 && got[13] == 0xB5 && !memcmp(got + 14, "raw frame", 9));
	close(dfd);
	close(cfd);
	snprintf(path, sizeof(path), "/net/ether0/%s", num);
	expect("the conversation goes when closed", access(path, F_OK) < 0);
}

int main(int argc, char **argv) {
	char buf[256], list[512], adir[NETPATHLEN], ldir[NETPATHLEN], dir[NETPATHLEN], st[40];
	int afd, lcfd, dfd, pid, status, fd, s, n, ucfd, ucfd2, udfd, udfd2;
	struct sockaddr_in a;
	if (argc > 2 && !strcmp(argv[1], "dial")) {
		fd = dial(argv[2], 0, 0, 0);
		if (fd < 0) {
			printf("dial %s failed\n", argv[2]);
			return 1;
		}
		while ((n = read(fd, buf, sizeof(buf))) > 0) write(1, buf, n);
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "ether")) {
		ether();
		printf("%s\n", fails ? "net9test: FAILED" : "net9test: all ok");
		return fails != 0;
	}
	listing("/net", list);
	printf("/net: %s\n", list);
	printf("cs tcp!localhost!7000: %s\n", cs("tcp!localhost!7000", buf, sizeof(buf)));
	printf("cs net!127.0.0.1!http: %s\n", cs("net!127.0.0.1!http", buf, sizeof(buf)));
	printf("cs udp!*!5200: %s\n", cs("udp!*!5200", buf, sizeof(buf)));
	printf("cs tcp!nosuchhost!1: %s\n", cs("tcp!nosuchhost!1", buf, sizeof(buf)));
	printf("cs tcp!localhost!nosuchservice: %s\n", cs("tcp!localhost!nosuchservice", buf, sizeof(buf)));
	printf("netmkaddr: %s", netmkaddr("beta", "tcp", "17007"));
	printf(" %s", netmkaddr("tcp!beta", 0, "80"));
	printf(" %s\n", netmkaddr("udp!beta!53", "tcp", "1"));

	afd = announce("tcp!*!5100", adir);
	expect("announce tcp!*!5100", afd >= 0);
	field(adir, "status", st, sizeof(st));
	field(adir, "local", buf, sizeof(buf));
	printf("%s: %s, local %s\n", adir, st, buf);
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		char cdir[NETPATHLEN];
		int cfd, d = dial("tcp!127.0.0.1!5100", 0, cdir, &cfd);
		if (d < 0) _exit(1);
		if (write(d, "hello from dial", 15) != 15) _exit(2);
		n = read(d, buf, sizeof(buf) - 1);
		if (n != 6 || memcmp(buf, "thanks", 6)) _exit(3);
		_exit(0);
	}
	lcfd = listen9(adir, ldir);
	expect("listen9 gives a new conversation", lcfd >= 0 && strcmp(ldir, adir));
	dfd = accept9(lcfd, ldir);
	n = dfd >= 0 ? read(dfd, buf, sizeof(buf) - 1) : -1;
	expect("accept9 and read what the client wrote", n == 15 && !memcmp(buf, "hello from dial", 15));
	field(ldir, "status", st, sizeof(st));
	expect("its status is Established", !strcmp(st, "Established"));
	field(ldir, "local", buf, sizeof(buf));
	printf("local %s", buf);
	field(ldir, "remote", buf, sizeof(buf));
	printf(", remote %s\n", strncmp(buf, "127.0.0.1!", 10) ? buf : "127.0.0.1!(port)");
	expect("write back", write(dfd, "thanks", 6) == 6);
	waitpid(pid, &status, 0);
	expect("the dialer got the reply", WIFEXITED(status) && WEXITSTATUS(status) == 0);
	expect("fd2path of the data file", fd2path(dfd, buf, sizeof(buf)) == 0 && !strncmp(buf, "/net/tcp/", 9) && strstr(buf, "/data"));
	expect("hangup", hangup(lcfd) == 0);
	close(dfd);
	close(lcfd);

	s = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(5100);
	a.sin_addr.s_addr = inet_addr("127.0.0.1");
	expect("socket() connects to the announced port", connect(s, (struct sockaddr *)&a, sizeof(a)) == 0);
	lcfd = listen9(adir, ldir);
	snprintf(dir, sizeof(dir), "/net/tcp/%d", 0);
	{
		DIR *d = opendir("/net/tcp");
		struct dirent *e;
		int found = 0;
		while (d && (e = readdir(d)) != NULL) {
			char p[64], r[40];
			if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
			snprintf(p, sizeof(p), "/net/tcp/%s", e->d_name);
			field(p, "remote", r, sizeof(r));
			field(p, "status", st, sizeof(st));
			if (!strcmp(r, "127.0.0.1!5100") && !strcmp(st, "Established")) found = 1;
		}
		if (d) closedir(d);
		expect("the socket() connection is in /net/tcp", found);
	}
	dfd = accept9(lcfd, ldir);
	expect("socket() writes, /net reads", write(s, "bsd", 3) == 3 && read(dfd, buf, 10) == 3 && !memcmp(buf, "bsd", 3));
	expect("hangup gives the socket end of file", hangup(lcfd) == 0 && read(s, buf, 10) == 0);
	close(s);
	close(dfd);
	close(lcfd);
	close(afd);

	ucfd = open("/net/udp/clone", O_RDWR);
	n = read(ucfd, buf, 10);
	buf[n > 0 ? n : 0] = 0;
	snprintf(dir, sizeof(dir), "/net/udp/%s", buf);
	expect("udp announce", write(ucfd, "announce 5200", 13) == 13);
	ucfd2 = open("/net/udp/clone", O_RDWR);
	n = read(ucfd2, buf, 10);
	buf[n > 0 ? n : 0] = 0;
	snprintf(ldir, sizeof(ldir), "/net/udp/%s", buf);
	expect("udp connect", write(ucfd2, "connect 127.0.0.1!5200", 22) == 22);
	snprintf(buf, sizeof(buf), "%s/data", dir);
	udfd = open(buf, O_RDWR);
	snprintf(buf, sizeof(buf), "%s/data", ldir);
	udfd2 = open(buf, O_RDWR);
	expect("a datagram through /net/udp", write(udfd2, "datagram", 8) == 8 && read(udfd, buf, sizeof(buf)) == 8 && !memcmp(buf, "datagram", 8));
	field(dir, "status", st, sizeof(st));
	printf("udp status %s\n", st);
	close(udfd);
	close(udfd2);
	close(ucfd);
	close(ucfd2);

	fd = open("/net/tcp/clone", O_RDWR);
	expect("connect to a closed port fails", fd >= 0 && write(fd, "connect 127.0.0.1!5999", 22) < 0 && errno == ECONNREFUSED);
	n = read(fd, buf, 10);
	buf[n > 0 ? n : 0] = 0;
	snprintf(dir, sizeof(dir), "/net/tcp/%s", buf);
	field(dir, "err", st, sizeof(st));
	printf("err: %s\n", st);
	expect("a bad control message", write(fd, "frobnicate", 10) < 0 && errno == EINVAL);
	buf[0] = 0;
	errstr(buf, sizeof(buf));
	printf("errstr: %s\n", buf);
	close(fd);
	expect("the conversation goes when its files close", access(dir, F_OK) < 0);
	printf("%s\n", fails ? "net9test: FAILED" : "net9test: all ok");
	return fails != 0;
}
