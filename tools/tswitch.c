/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXMAC 256
#define MAXPORTS 64

typedef struct {
	unsigned char mac[6];
	struct sockaddr_in addr;
} Entry;

static Entry table[MAXMAC];
static int nent;
static struct sockaddr_in ports[MAXPORTS];
static int nports;
static int verbose;

static int same(const struct sockaddr_in *a, const struct sockaddr_in *b) {
	return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void learn(const unsigned char *mac, const struct sockaddr_in *from) {
	int i;
	if (mac[0] & 1) return;
	for (i = 0; i < nent; i++)
		if (!memcmp(table[i].mac, mac, 6)) {
			table[i].addr = *from;
			return;
		}
	if (nent < MAXMAC) {
		memcpy(table[nent].mac, mac, 6);
		table[nent].addr = *from;
		nent++;
		if (verbose) fprintf(stderr, "tswitch: learned %02x:%02x:%02x:%02x:%02x:%02x at %s:%d\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], inet_ntoa(from->sin_addr), ntohs(from->sin_port));
	}
}

static void add_port(const struct sockaddr_in *from) {
	int i;
	for (i = 0; i < nports; i++)
		if (same(&ports[i], from)) return;
	if (nports < MAXPORTS) ports[nports++] = *from;
}

static const struct sockaddr_in *lookup(const unsigned char *mac) {
	int i;
	if (mac[0] & 1) return NULL;
	for (i = 0; i < nent; i++)
		if (!memcmp(table[i].mac, mac, 6)) return &table[i].addr;
	return NULL;
}

int main(int argc, char **argv) {
	struct sockaddr_in local, from;
	unsigned char buf[2048];
	int fd, i, port;
	if (argc > 1 && !strcmp(argv[1], "-v")) {
		verbose = 1;
		argv++;
		argc--;
	}
	if (argc != 2 || (port = atoi(argv[1])) <= 0 || port > 65535) {
		fprintf(stderr, "usage: tswitch [-v] PORT\n");
		return 1;
	}
	signal(SIGPIPE, SIG_IGN);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = htons((unsigned short)port);
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	if (fd < 0 || bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
		perror("tswitch");
		return 1;
	}
	for (;;) {
		socklen_t fl = sizeof(from);
		ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		const struct sockaddr_in *to;
		if (n < 14) continue;
		add_port(&from);
		learn(buf + 6, &from);
		to = lookup(buf);
		if (to) {
			if (!same(to, &from)) sendto(fd, buf, (size_t)n, 0, (const struct sockaddr *)to, sizeof(*to));
			continue;
		}
		for (i = 0; i < nports; i++)
			if (!same(&ports[i], &from)) sendto(fd, buf, (size_t)n, 0, (struct sockaddr *)&ports[i], sizeof(ports[i]));
	}
}
