/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

static int sock;

static unsigned get_addr(struct ifreq *r) {
	unsigned char *b = (unsigned char *)r->ifr_data;
	return (unsigned)b[0] << 24 | (unsigned)b[1] << 16 | (unsigned)b[2] << 8 | b[3];
}

static void set_addr(struct ifreq *r, unsigned a) {
	unsigned char *b = (unsigned char *)r->ifr_data;
	memset(r->ifr_data, 0, sizeof(r->ifr_data));
	b[0] = (unsigned char)(a >> 24);
	b[1] = (unsigned char)(a >> 16);
	b[2] = (unsigned char)(a >> 8);
	b[3] = (unsigned char)a;
}

static char *dotted(unsigned a) {
	static char buf[2][16];
	static int k;
	k ^= 1;
	snprintf(buf[k], 16, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
	return buf[k];
}

static void counters(const char *name) {
	FILE *f = fopen("/proc/net/dev", "r");
	char line[128], n[16];
	unsigned rx, tx, drops;
	if (!f) return;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%15s %u %u %u", n, &rx, &tx, &drops) == 4 && !strcmp(n, name)) printf("\trx %u tx %u drops %u\n", rx, tx, drops);
	fclose(f);
}

static void show(const char *name) {
	struct ifreq r;
	unsigned flags, addr, mask;
	unsigned char *m;
	memset(&r, 0, sizeof(r));
	strncpy(r.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(sock, SIOCGIFFLAGS, &r) < 0) {
		fprintf(stderr, "ifconfig: %s: no such interface\n", name);
		return;
	}
	flags = ((unsigned char *)r.ifr_data)[0];
	ioctl(sock, SIOCGIFADDR, &r);
	addr = get_addr(&r);
	ioctl(sock, SIOCGIFNETMASK, &r);
	mask = get_addr(&r);
	printf("%s: flags=%s%s%s\n", name, flags & IFF_UP ? "UP" : "DOWN", flags & IFF_LOOPBACK ? ",LOOPBACK" : "", flags & IFF_RUNNING ? ",RUNNING" : "");
	printf("\tinet %s netmask %s\n", dotted(addr), dotted(mask));
	ioctl(sock, SIOCGIFHWADDR, &r);
	m = (unsigned char *)r.ifr_data;
	if (!(flags & IFF_LOOPBACK)) printf("\tether %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
	counters(name);
}

static int parse(const char *s, unsigned *a) {
	struct in_addr in;
	if (!inet_aton(s, &in)) {
		fprintf(stderr, "ifconfig: bad address %s\n", s);
		return -1;
	}
	*a = ntohl(in.s_addr);
	return 0;
}

int main(int argc, char **argv) {
	struct ifreq r;
	int i = 2;
	unsigned a;
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (argc < 2) {
		show("lo");
		show("eth0");
		return 0;
	}
	if (argc == 2) {
		show(argv[1]);
		return 0;
	}
	memset(&r, 0, sizeof(r));
	strncpy(r.ifr_name, argv[1], IFNAMSIZ - 1);
	if (ioctl(sock, SIOCGIFFLAGS, &r) < 0) {
		fprintf(stderr, "ifconfig: %s: no such interface\n", argv[1]);
		return 1;
	}
	if (i < argc && strcmp(argv[i], "up") && strcmp(argv[i], "down") && strcmp(argv[i], "netmask")) {
		if (parse(argv[i], &a) < 0) return 1;
		set_addr(&r, a);
		if (ioctl(sock, SIOCSIFADDR, &r) < 0) {
			perror("ifconfig: SIOCSIFADDR");
			return 1;
		}
		i++;
	}
	for (; i < argc; i++) {
		if (!strcmp(argv[i], "netmask") && i + 1 < argc) {
			if (parse(argv[++i], &a) < 0) return 1;
			set_addr(&r, a);
			if (ioctl(sock, SIOCSIFNETMASK, &r) < 0) {
				perror("ifconfig: SIOCSIFNETMASK");
				return 1;
			}
		} else if (!strcmp(argv[i], "up") || !strcmp(argv[i], "down")) {
			ioctl(sock, SIOCGIFFLAGS, &r);
			if (argv[i][0] == 'u') ((unsigned char *)r.ifr_data)[0] |= IFF_UP;
			else ((unsigned char *)r.ifr_data)[0] &= (unsigned char)~IFF_UP;
			if (ioctl(sock, SIOCSIFFLAGS, &r) < 0) {
				perror("ifconfig: SIOCSIFFLAGS");
				return 1;
			}
		} else {
			fprintf(stderr, "usage: ifconfig [if [addr [netmask mask]] [up|down]]\n");
			return 1;
		}
	}
	return 0;
}
