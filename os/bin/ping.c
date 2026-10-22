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
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static unsigned now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 100 + ts.tv_nsec / 10000000;
}

static unsigned short cksum(unsigned char *p, int n) {
	unsigned s = 0;
	int i;
	for (i = 0; i + 1 < n; i += 2) s += (unsigned)(p[i] << 8 | p[i + 1]);
	if (n & 1) s += (unsigned)(p[n - 1] << 8);
	while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
	return (unsigned short)~s;
}

int main(int argc, char **argv) {
	int i = 1, count = 4, s, seq, sent = 0, got = 0, id = getpid() & 0xFFFF;
	struct sockaddr_in to;
	struct hostent *h;
	unsigned char pkt[64], buf[256];
	char addr[16];
	if (i + 1 < argc && !strcmp(argv[i], "-c")) {
		count = atoi(argv[i + 1]);
		i += 2;
	}
	if (i >= argc) {
		fprintf(stderr, "usage: ping [-c n] host\n");
		return 2;
	}
	h = gethostbyname(argv[i]);
	if (!h) {
		fprintf(stderr, "ping: unknown host %s\n", argv[i]);
		return 2;
	}
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	memcpy(&to.sin_addr, h->h_addr, 4);
	strcpy(addr, inet_ntoa(to.sin_addr));
	s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (s < 0) {
		perror("ping: socket");
		return 2;
	}
	printf("PING %s (%s): 56 data bytes\n", argv[i], addr);
	for (seq = 1; seq <= count; seq++) {
		unsigned start = now(), sum;
		struct pollfd pf;
		int k, done = 0;
		memset(pkt, 0, sizeof(pkt));
		pkt[0] = 8;
		pkt[4] = (unsigned char)(id >> 8);
		pkt[5] = (unsigned char)id;
		pkt[6] = (unsigned char)(seq >> 8);
		pkt[7] = (unsigned char)seq;
		for (k = 8; k < 64; k++) pkt[k] = (unsigned char)k;
		sum = cksum(pkt, 64);
		pkt[2] = (unsigned char)(sum >> 8);
		pkt[3] = (unsigned char)sum;
		if (sendto(s, pkt, 64, 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
			printf("ping: sendto: %s\n", strerror(errno));
		} else {
			sent++;
		}
		while (!done) {
			int left = (int)(start + 100) - (int)now(), r;
			if (left <= 0) break;
			pf.fd = s;
			pf.events = POLLIN;
			if (poll(&pf, 1, left) <= 0) break;
			r = recv(s, buf, sizeof(buf), 0);
			if (r >= 28 && buf[20] == 0 && buf[24] == pkt[4] && buf[25] == pkt[5] && buf[26] == pkt[6] && buf[27] == pkt[7]) {
				struct in_addr from;
				memcpy(&from, buf + 12, 4);
				printf("%d bytes from %s: icmp_seq=%d time=%u ticks\n", r - 20, inet_ntoa(from), seq, now() - start);
				got++;
				done = 1;
			}
		}
		if (seq < count) {
			int left = (int)(start + 100) - (int)now();
			if (left > 0) usleep((unsigned)left * 10000);
		}
	}
	printf("--- %s ping statistics ---\n", argv[i]);
	printf("%d packets transmitted, %d received, %d%% packet loss\n", sent, got, sent ? (sent - got) * 100 / sent : 0);
	return got ? 0 : 1;
}
