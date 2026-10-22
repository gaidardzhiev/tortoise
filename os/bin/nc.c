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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int resolve(const char *host, int port, struct sockaddr_in *a) {
	struct hostent *h = gethostbyname(host);
	if (!h) {
		fprintf(stderr, "nc: unknown host %s\n", host);
		return -1;
	}
	memset(a, 0, sizeof(*a));
	a->sin_family = AF_INET;
	a->sin_port = htons((unsigned short)port);
	memcpy(&a->sin_addr, h->h_addr, 4);
	return 0;
}

static int relay(int s, int udp, struct sockaddr_in *peer, int have_peer, int server) {
	struct pollfd pf[2];
	char buf[1024];
	int in_open = 1;
	for (;;) {
		int n = 0, r;
		pf[n].fd = s;
		pf[n].events = POLLIN;
		n++;
		if (in_open) {
			pf[n].fd = 0;
			pf[n].events = POLLIN;
			n++;
		}
		if (poll(pf, n, -1) < 0) {
			if (errno == EINTR) continue;
			return 1;
		}
		if (pf[0].revents) {
			socklen_t len = sizeof(*peer);
			r = udp ? recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)peer, &len) : read(s, buf, sizeof(buf));
			if (r < 0) {
				fprintf(stderr, "nc: %s\n", strerror(errno));
				return 1;
			}
			if (r == 0 && !udp) return 0;
			if (udp) have_peer = 1;
			write(1, buf, r);
		}
		if (in_open && n > 1 && pf[1].revents) {
			r = read(0, buf, sizeof(buf));
			if (r <= 0) {
				in_open = 0;
				if (udp && !server) return 0;
				if (!udp && !server) shutdown(s, SHUT_WR);
				continue;
			}
			if (udp) {
				if (have_peer) sendto(s, buf, r, 0, (struct sockaddr *)peer, sizeof(*peer));
			} else if (write(s, buf, r) < 0) {
				fprintf(stderr, "nc: %s\n", strerror(errno));
				return 1;
			}
		}
	}
}

int main(int argc, char **argv) {
	int i = 1, listen_mode = 0, udp = 0, s, one = 1;
	struct sockaddr_in a;
	signal(SIGPIPE, SIG_IGN);
	while (i < argc && argv[i][0] == '-') {
		char *o;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'l') listen_mode = 1;
			else if (*o == 'u') udp = 1;
		}
		i++;
	}
	if ((listen_mode && i + 1 != argc) || (!listen_mode && i + 2 != argc)) {
		fprintf(stderr, "usage: nc [-u] host port | nc [-u] -l port\n");
		return 2;
	}
	s = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
	if (s < 0) {
		perror("nc: socket");
		return 1;
	}
	if (listen_mode) {
		int c;
		memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET;
		a.sin_port = htons((unsigned short)atoi(argv[i]));
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
			fprintf(stderr, "nc: bind: %s\n", strerror(errno));
			return 1;
		}
		if (udp) return relay(s, 1, &a, 0, 1);
		listen(s, 1);
		c = accept(s, NULL, NULL);
		if (c < 0) {
			fprintf(stderr, "nc: accept: %s\n", strerror(errno));
			return 1;
		}
		close(s);
		return relay(c, 0, &a, 1, 1);
	}
	if (resolve(argv[i], atoi(argv[i + 1]), &a) < 0) return 1;
	if (connect(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
		fprintf(stderr, "nc: %s\n", strerror(errno));
		return 1;
	}
	return relay(s, udp, &a, 1, 0);
}
