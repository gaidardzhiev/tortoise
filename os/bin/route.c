/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>

static unsigned be(unsigned a) {
	unsigned char b[4];
	b[0] = (unsigned char)(a >> 24);
	b[1] = (unsigned char)(a >> 16);
	b[2] = (unsigned char)(a >> 8);
	b[3] = (unsigned char)a;
	return *(unsigned *)b;
}

int main(int argc, char **argv) {
	struct rtentry rt;
	struct in_addr in;
	unsigned dst = 0, mask = 0, gw = 0;
	int s, i, add;
	if (argc < 2) {
		FILE *f = fopen("/proc/net/route", "r");
		char line[128];
		if (!f) {
			fprintf(stderr, "route: cannot read /proc/net/route\n");
			return 1;
		}
		printf("%-15s %-15s %-15s %s\n", "Destination", "Netmask", "Gateway", "Iface");
		while (fgets(line, sizeof(line), f)) {
			char a[16], b[16], c[16], d[16];
			if (sscanf(line, "%15s %15s %15s %15s", a, b, c, d) == 4) printf("%-15s %-15s %-15s %s\n", a, b, c, d);
		}
		fclose(f);
		return 0;
	}
	add = !strcmp(argv[1], "add");
	if ((!add && strcmp(argv[1], "del")) || argc < 3) {
		fprintf(stderr, "usage: route [add|del default|net/bits gw addr]\n");
		return 1;
	}
	if (strcmp(argv[2], "default")) {
		char net[32], *slash;
		int bits = 32;
		strncpy(net, argv[2], sizeof(net) - 1);
		net[sizeof(net) - 1] = 0;
		slash = strchr(net, '/');
		if (slash) {
			*slash = 0;
			bits = atoi(slash + 1);
		}
		if (!inet_aton(net, &in) || bits < 0 || bits > 32) {
			fprintf(stderr, "route: bad network %s\n", argv[2]);
			return 1;
		}
		dst = ntohl(in.s_addr);
		mask = bits ? 0xFFFFFFFFu << (32 - bits) : 0;
	}
	for (i = 3; i < argc; i++)
		if (!strcmp(argv[i], "gw") && i + 1 < argc) {
			if (!inet_aton(argv[++i], &in)) {
				fprintf(stderr, "route: bad gateway %s\n", argv[i]);
				return 1;
			}
			gw = ntohl(in.s_addr);
		}
	memset(&rt, 0, sizeof(rt));
	rt.rt_dst = be(dst);
	rt.rt_mask = be(mask);
	rt.rt_gateway = be(gw);
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (ioctl(s, add ? SIOCADDRT : SIOCDELRT, &rt) < 0) {
		perror("route");
		return 1;
	}
	return 0;
}
