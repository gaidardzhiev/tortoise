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
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <termios.h>

#define TIOCGPTN 0x80045430u

static const char *program = "/bin/login";

static void session(int c) {
	int m = open("/dev/ptmx", O_RDWR), n = 0, pid, net_open = 1;
	char path[32], buf[1024];
	if (m < 0 || ioctl(m, TIOCGPTN, &n) < 0) {
		write(c, "telnetd: no pseudo terminal\r\n", 29);
		return;
	}
	snprintf(path, sizeof(path), "/dev/pts/%d", n);
	pid = fork();
	if (pid == 0) {
		int s;
		close(c);
		close(m);
		setsid();
		s = open(path, O_RDWR);
		if (s < 0) _exit(1);
		ioctl(s, TIOCSCTTY, 0);
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2) close(s);
		signal(SIGPIPE, SIG_DFL);
		execl(program, strrchr(program, '/') ? strrchr(program, '/') + 1 : program, (char *)NULL);
		_exit(127);
	}
	for (;;) {
		struct pollfd pf[2];
		int k = 0, r;
		pf[k].fd = m;
		pf[k].events = POLLIN;
		k++;
		if (net_open) {
			pf[k].fd = c;
			pf[k].events = POLLIN;
			k++;
		}
		if (poll(pf, k, -1) < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (pf[0].revents) {
			r = read(m, buf, sizeof(buf));
			if (r <= 0) break;
			if (write(c, buf, r) < 0) break;
		}
		if (net_open && k > 1 && pf[1].revents) {
			r = read(c, buf, sizeof(buf));
			if (r <= 0) net_open = 0;
			else write(m, buf, r);
		}
	}
	close(m);
	kill(pid, SIGHUP);
	waitpid(pid, NULL, 0);
}

int main(int argc, char **argv) {
	int i = 1, port = 23, s, one = 1;
	struct sockaddr_in a;
	while (i + 1 < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-p")) port = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-l")) program = argv[i + 1];
		i += 2;
	}
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
	s = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)port);
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 4) < 0) {
		fprintf(stderr, "telnetd: port %d: %s\n", port, strerror(errno));
		return 1;
	}
	for (;;) {
		int c = accept(s, NULL, NULL), pid;
		if (c < 0) {
			if (errno == EINTR) continue;
			return 1;
		}
		pid = fork();
		if (pid == 0) {
			close(s);
			signal(SIGCHLD, SIG_DFL);
			session(c);
			_exit(0);
		}
		close(c);
	}
}
