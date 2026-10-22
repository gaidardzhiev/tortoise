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
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cpu.h"
#include "dev.h"

#define NQ 64
#define FRAME_MIN 14
#define FRAME_MAX 1514
#define RXBUF_SIZE 1536

#define B_NONE 0
#define B_LOOP 1
#define B_UDP 2

typedef struct {
	int len;
	uint8_t d[FRAME_MAX];
} Frame;

static struct {
	uint32_t ctrl, status, tx_addr, tx_len, rx_addr, rx_len, stx, srx, sdrop;
	int tx_pending;
	uint64_t tx_done;
	int txlen;
	uint8_t txbuf[FRAME_MAX];
	Frame q[NQ];
	int qh, qn;
} nd;

static uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
static int backend;
static int udpfd = -1;
static struct sockaddr_in remote;
static FILE *pcap;

static void put32le(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void pcap_frame(const uint8_t *d, int len) {
	uint8_t h[16];
	if (!pcap) return;
	put32le(h, (uint32_t)(cpu.icount / 1000000));
	put32le(h + 4, (uint32_t)(cpu.icount % 1000000));
	put32le(h + 8, (uint32_t)len);
	put32le(h + 12, (uint32_t)len);
	fwrite(h, 1, 16, pcap);
	fwrite(d, 1, (size_t)len, pcap);
	fflush(pcap);
}

static int parse_mac(const char *s, uint8_t *out) {
	unsigned v[6];
	int i;
	if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return -1;
	for (i = 0; i < 6; i++) {
		if (v[i] > 255) return -1;
		out[i] = (uint8_t)v[i];
	}
	return 0;
}

int net_config(const char *spec, const char *macstr, const char *pcap_path) {
	if (macstr && parse_mac(macstr, mac)) {
		fprintf(stderr, "bad MAC address %s\n", macstr);
		return -1;
	}
	if (udpfd >= 0) {
		close(udpfd);
		udpfd = -1;
	}
	backend = B_NONE;
	if (spec && !strcmp(spec, "loop")) {
		backend = B_LOOP;
	} else if (spec && !strncmp(spec, "udp:", 4)) {
		char host[256];
		unsigned lport, rport;
		struct sockaddr_in local;
		if (sscanf(spec + 4, "%u:%255[^:]:%u", &lport, host, &rport) != 3 || lport > 65535 || rport > 65535) {
			fprintf(stderr, "bad network backend %s\n", spec);
			return -1;
		}
		udpfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (udpfd < 0) {
			perror("socket");
			return -1;
		}
		memset(&local, 0, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_port = htons((uint16_t)lport);
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(udpfd, (struct sockaddr *)&local, sizeof(local)) < 0) {
			perror("bind");
			close(udpfd);
			udpfd = -1;
			return -1;
		}
		memset(&remote, 0, sizeof(remote));
		remote.sin_family = AF_INET;
		remote.sin_port = htons((uint16_t)rport);
		if (!strcmp(host, "localhost")) strcpy(host, "127.0.0.1");
		if (inet_aton(host, &remote.sin_addr) == 0) {
			fprintf(stderr, "bad host address %s\n", host);
			return -1;
		}
		fcntl(udpfd, F_SETFL, O_NONBLOCK);
		backend = B_UDP;
	} else if (spec && strcmp(spec, "none")) {
		fprintf(stderr, "bad network backend %s\n", spec);
		return -1;
	}
	if (pcap) {
		fclose(pcap);
		pcap = NULL;
	}
	if (pcap_path) {
		uint8_t h[24];
		pcap = fopen(pcap_path, "wb");
		if (!pcap) {
			perror(pcap_path);
			return -1;
		}
		put32le(h, 0xA1B2C3D4u);
		h[4] = 2;
		h[5] = 0;
		h[6] = 4;
		h[7] = 0;
		put32le(h + 8, 0);
		put32le(h + 12, 0);
		put32le(h + 16, 65535);
		put32le(h + 20, 1);
		fwrite(h, 1, 24, pcap);
		fflush(pcap);
	}
	return 0;
}

static void try_deliver(void) {
	Frame *f;
	if (!(nd.ctrl & 1) || (nd.status & 1) || !nd.rx_addr || nd.qn == 0) return;
	if ((uint64_t)nd.rx_addr + RXBUF_SIZE > cpu.ram_size) return;
	f = &nd.q[nd.qh];
	memcpy(cpu.memory + nd.rx_addr, f->d, (size_t)f->len);
	cpu_mem_written(nd.rx_addr, (uint32_t)f->len);
	nd.rx_len = (uint32_t)f->len;
	nd.status |= 1;
	nd.qh = (nd.qh + 1) % NQ;
	nd.qn--;
	nd.srx++;
	if (nd.ctrl & 2) pic_raise(IRQ_NET);
}

static void receive(const uint8_t *d, int len) {
	static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	Frame *f;
	if (len < FRAME_MIN || len > FRAME_MAX) return;
	if (!(nd.ctrl & 8) && memcmp(d, mac, 6) && memcmp(d, bcast, 6)) return;
	if (!(nd.ctrl & 1) || nd.qn == NQ) {
		nd.sdrop++;
		return;
	}
	pcap_frame(d, len);
	f = &nd.q[(nd.qh + nd.qn) % NQ];
	memcpy(f->d, d, (size_t)len);
	f->len = len;
	nd.qn++;
	try_deliver();
}

void net_inject(const uint8_t *d, int len) {
	receive(d, len);
}

static uint32_t net_rd(uint32_t off) {
	switch (off) {
	case 0x00: return mac[0] | (uint32_t)mac[1] << 8 | (uint32_t)mac[2] << 16 | (uint32_t)mac[3] << 24;
	case 0x04: return mac[4] | (uint32_t)mac[5] << 8;
	case 0x08: return nd.ctrl;
	case 0x0C: return (nd.status & 1) | (nd.tx_pending ? 2 : 0) | (backend != B_NONE ? 4 : 0);
	case 0x10: return nd.tx_addr;
	case 0x14: return nd.tx_len;
	case 0x18: return nd.rx_addr;
	case 0x1C: return nd.rx_len;
	case 0x20: return nd.stx;
	case 0x24: return nd.srx;
	case 0x28: return nd.sdrop;
	}
	return 0;
}

static void net_wr(uint32_t off, uint32_t v) {
	switch (off) {
	case 0x08:
		nd.ctrl = v & 15;
		try_deliver();
		break;
	case 0x0C:
		if (v & 1) {
			nd.status &= ~1u;
			try_deliver();
		}
		break;
	case 0x10:
		nd.tx_addr = v;
		break;
	case 0x14:
		nd.tx_len = v;
		if (!(nd.ctrl & 1) || nd.tx_pending || v < FRAME_MIN || v > FRAME_MAX || (uint64_t)nd.tx_addr + v > cpu.ram_size) break;
		memcpy(nd.txbuf, cpu.memory + nd.tx_addr, v);
		nd.txlen = (int)v;
		nd.tx_pending = 1;
		nd.tx_done = cpu.icount + (v + 15) / 16;
		dev_reschedule();
		break;
	case 0x18:
		nd.rx_addr = v;
		try_deliver();
		break;
	}
}

void net_reset(void) {
	memset(&nd, 0, sizeof(nd));
	dev_add(NET_BASE, net_rd, net_wr);
}

uint64_t net_next(void) {
	return nd.tx_pending ? nd.tx_done : UINT64_MAX;
}

void net_poll(void) {
	uint8_t buf[2048];
	ssize_t n;
	if (backend != B_UDP) return;
	while ((n = recv(udpfd, buf, sizeof(buf), MSG_DONTWAIT)) > 0) receive(buf, (int)n);
}

void net_events(void) {
	if (nd.tx_pending && cpu.icount >= nd.tx_done) {
		nd.tx_pending = 0;
		nd.stx++;
		pcap_frame(nd.txbuf, nd.txlen);
		if (backend == B_LOOP) receive(nd.txbuf, nd.txlen);
		else if (backend == B_UDP) sendto(udpfd, nd.txbuf, (size_t)nd.txlen, 0, (struct sockaddr *)&remote, sizeof(remote));
		if (nd.ctrl & 4) pic_raise(IRQ_NET);
	}
}

int net_wait_fd(void) {
	if (backend != B_UDP || !(nd.ctrl & 1) || !(nd.ctrl & 2) || !(pic_mask & (1u << IRQ_NET))) return -1;
	return udpfd;
}

void net_close(void) {
	if (pcap) fclose(pcap);
	pcap = NULL;
	if (udpfd >= 0) close(udpfd);
	udpfd = -1;
	backend = B_NONE;
}
