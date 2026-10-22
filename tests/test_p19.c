/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "tutil.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define RAM 0x100000
#define N(off) (NET_BASE + (off))

static uint32_t rreg(uint32_t off) {
	uint32_t v = 0;
	dev_access(N(off), &v, 0);
	return v;
}

static void wreg(uint32_t off, uint32_t v) {
	dev_access(N(off), &v, 1);
}

static void advance(uint64_t n) {
	cpu.icount += n;
	dev_events();
}

static void frame(uint8_t *p, const uint8_t *dst, int len, int seed) {
	static const uint8_t src[6] = {2, 0, 0, 0, 0, 9};
	int i;
	memcpy(p, dst, 6);
	memcpy(p + 6, src, 6);
	p[12] = 0x88;
	p[13] = 0xB5;
	for (i = 14; i < len; i++) p[i] = (uint8_t)(i * 7 + seed);
}

static void put32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

int main(void) {
	static const uint8_t me[6] = {2, 0, 0, 0, 0, 1};
	static const uint8_t other[6] = {2, 0, 0, 0, 0, 7};
	static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	uint8_t f[1600], expect[512], got[512];
	int i, n, delivered;
	FILE *pf;
	cpu_init_size(RAM);
	CHECK(net_config("none", NULL, NULL) == 0);
	CHECKV(rreg(0x00) == 0x00000002, rreg(0x00), 2);
	CHECKV(rreg(0x04) == 0x0100, rreg(0x04), 0x100);
	CHECK((rreg(0x0C) & 4) == 0);
	CHECK(net_config("none", "0a:0b:0c:0d:0e:0f", NULL) == 0);
	cpu_init_size(RAM);
	CHECKV(rreg(0x00) == 0x0D0C0B0A, rreg(0x00), 0x0D0C0B0A);
	CHECKV(rreg(0x04) == 0x0F0E, rreg(0x04), 0x0F0E);
	CHECK(net_config("loop", "02:00:00:00:00:01", NULL) == 0);
	CHECK(net_config("bogus", NULL, NULL) != 0);
	CHECK(net_config("udp:1:2", NULL, NULL) != 0);
	CHECK(net_config("loop", "zz", NULL) != 0);
	CHECK(net_config("loop", NULL, NULL) == 0);
	cpu_init_size(RAM);
	CHECK((rreg(0x0C) & 4) == 4);

	frame(cpu.memory + 0x1000, me, 60, 1);
	wreg(0x10, 0x1000);
	wreg(0x14, 60);
	advance(100);
	CHECK(rreg(0x20) == 0);
	wreg(0x08, 7);
	wreg(0x18, 0x2000);
	pic_pending = 0;
	wreg(0x14, 60);
	CHECK((rreg(0x0C) & 2) == 2);
	advance(3);
	CHECK((rreg(0x0C) & 2) == 2);
	advance(1);
	CHECK((rreg(0x0C) & 2) == 0);
	CHECK(rreg(0x20) == 1 && rreg(0x24) == 1 && rreg(0x28) == 0);
	CHECK(rreg(0x1C) == 60 && (rreg(0x0C) & 1) == 1);
	CHECK(memcmp(cpu.memory + 0x2000, cpu.memory + 0x1000, 60) == 0);
	CHECK(pic_pending & (1u << IRQ_NET));
	wreg(0x0C, 1);
	CHECK((rreg(0x0C) & 1) == 0);

	frame(f, other, 64, 2);
	net_inject(f, 64);
	CHECK((rreg(0x0C) & 1) == 0 && rreg(0x24) == 1);
	frame(f, bcast, 64, 3);
	net_inject(f, 64);
	CHECK((rreg(0x0C) & 1) == 1 && rreg(0x24) == 2 && cpu.memory[0x2000] == 0xFF);
	wreg(0x0C, 1);
	wreg(0x08, 15);
	frame(f, other, 70, 4);
	net_inject(f, 70);
	CHECK((rreg(0x0C) & 1) == 1 && rreg(0x1C) == 70);
	wreg(0x0C, 1);
	wreg(0x08, 7);
	frame(f, me, 13, 5);
	net_inject(f, 13);
	CHECK((rreg(0x0C) & 1) == 0);
	wreg(0x14, 13);
	wreg(0x14, 1515);
	CHECK((rreg(0x0C) & 2) == 0 && rreg(0x20) == 1);

	for (i = 0; i < 70; i++) {
		frame(f, me, 100, i);
		net_inject(f, 100);
	}
	CHECKV(rreg(0x28) == 5, rreg(0x28), 5);
	delivered = 0;
	for (i = 0; i < 80; i++) {
		if (!(rreg(0x0C) & 1)) break;
		delivered++;
		wreg(0x0C, 1);
	}
	CHECKV(delivered == 65, delivered, 65);
	wreg(0x08, 0);
	frame(f, me, 100, 0);
	net_inject(f, 100);
	CHECK(rreg(0x28) == 6);

	CHECK(net_config("loop", NULL, "tests/net.tmp.pcap") == 0);
	cpu_init_size(RAM);
	cpu.icount = 2500000;
	wreg(0x08, 3);
	wreg(0x18, 0x3000);
	frame(cpu.memory + 0x1000, me, 20, 6);
	wreg(0x10, 0x1000);
	wreg(0x14, 20);
	advance(2);
	net_config("none", NULL, NULL);
	n = 0;
	put32(expect, 0xA1B2C3D4u);
	expect[4] = 2;
	expect[5] = 0;
	expect[6] = 4;
	expect[7] = 0;
	put32(expect + 8, 0);
	put32(expect + 12, 0);
	put32(expect + 16, 65535);
	put32(expect + 20, 1);
	n = 24;
	for (i = 0; i < 2; i++) {
		put32(expect + n, 2);
		put32(expect + n + 4, 500002);
		put32(expect + n + 8, 20);
		put32(expect + n + 12, 20);
		memcpy(expect + n + 16, cpu.memory + 0x1000, 20);
		n += 36;
	}
	pf = fopen("tests/net.tmp.pcap", "rb");
	CHECK(pf != NULL);
	if (pf) {
		int m = (int)fread(got, 1, sizeof(got), pf);
		fclose(pf);
		CHECKV(m == n, m, n);
		CHECK(memcmp(got, expect, (size_t)n) == 0);
	}
	remove("tests/net.tmp.pcap");

	{
		int s = socket(AF_INET, SOCK_DGRAM, 0);
		struct sockaddr_in a, to;
		char spec[64];
		uint8_t buf[2048];
		ssize_t r;
		memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		a.sin_port = 0;
		bind(s, (struct sockaddr *)&a, sizeof(a));
		{
			socklen_t l = sizeof(a);
			getsockname(s, (struct sockaddr *)&a, &l);
		}
		snprintf(spec, sizeof(spec), "udp:%d:127.0.0.1:%d", 47000 + (getpid() % 1000), ntohs(a.sin_port));
		CHECK(net_config(spec, NULL, NULL) == 0);
		cpu_init_size(RAM);
		wreg(0x08, 3);
		wreg(0x18, 0x2000);
		frame(cpu.memory + 0x1000, other, 80, 8);
		wreg(0x10, 0x1000);
		wreg(0x14, 80);
		advance(10);
		usleep(20000);
		r = recv(s, buf, sizeof(buf), MSG_DONTWAIT);
		CHECKV(r == 80, r, 80);
		CHECK(r == 80 && memcmp(buf, cpu.memory + 0x1000, 80) == 0);
		memset(&to, 0, sizeof(to));
		to.sin_family = AF_INET;
		to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		to.sin_port = htons((unsigned short)(47000 + (getpid() % 1000)));
		frame(buf, me, 90, 9);
		sendto(s, buf, 90, 0, (struct sockaddr *)&to, sizeof(to));
		usleep(20000);
		advance(2000);
		CHECK((rreg(0x0C) & 1) == 1 && rreg(0x1C) == 90 && memcmp(cpu.memory + 0x2000, buf, 90) == 0);
		close(s);
		net_config("none", NULL, NULL);
	}
	return report("test_p19");
}
