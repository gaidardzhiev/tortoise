/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char **argv) {
	struct sockaddr_in a;
	struct hostent *h;
	struct termios o, t;
	int s, tty, in_open = 1;
	char buf[1024];
	if (argc < 2) {
		fprintf(stderr, "usage: telnet host [port]\n");
		return 2;
	}
	h = gethostbyname(argv[1]);
	if (!h) {
		fprintf(stderr, "telnet: unknown host %s\n", argv[1]);
		return 1;
	}
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)(argc > 2 ? atoi(argv[2]) : 23));
	memcpy(&a.sin_addr, h->h_addr, 4);
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
		fprintf(stderr, "telnet: %s\n", strerror(errno));
		return 1;
	}
	printf("Connected to %s.\n", argv[1]);
	fflush(stdout);
	signal(SIGPIPE, SIG_IGN);
	tty = tcgetattr(0, &o) == 0;
	if (tty) {
		t = o;
		cfmakeraw(&t);
		tcsetattr(0, TCSANOW, &t);
	}
	for (;;) {
		struct pollfd pf[2];
		int k = 0, r;
		pf[k].fd = s;
		pf[k].events = POLLIN;
		k++;
		if (in_open) {
			pf[k].fd = 0;
			pf[k].events = POLLIN;
			k++;
		}
		if (poll(pf, k, -1) < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (pf[0].revents) {
			r = read(s, buf, sizeof(buf));
			if (r <= 0) break;
			write(1, buf, r);
		}
		if (in_open && k > 1 && pf[1].revents) {
			r = read(0, buf, sizeof(buf));
			if (r <= 0) {
				in_open = 0;
				shutdown(s, SHUT_WR);
			} else if (write(s, buf, r) < 0) {
				break;
			}
		}
	}
	if (tty) tcsetattr(0, TCSANOW, &o);
	printf("Connection closed.\n");
	return 0;
}
