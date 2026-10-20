/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cpu.h"
#include "dev.h"
#include "opts.h"

#define MAXBP 64
#define PKT 4096

static int sock = -1;
static uint32_t bps[MAXBP];
static int nbps;
static char pkt[PKT];

static int get_ch(void) {
	unsigned char c;
	if (read(sock, &c, 1) != 1) return -1;
	return c;
}

static int hexval(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int get_packet(void) {
	int c, n;
	for (;;) {
		unsigned sum = 0;
		do {
			c = get_ch();
			if (c < 0) return -1;
		} while (c != '$');
		n = 0;
		while ((c = get_ch()) >= 0 && c != '#') {
			if (n < PKT - 1) pkt[n++] = (char)c;
			sum += (unsigned)c;
		}
		if (c < 0) return -1;
		{
			int h = hexval(get_ch()), l = hexval(get_ch());
			pkt[n] = 0;
			if (h >= 0 && l >= 0 && (unsigned)(h * 16 + l) == (sum & 0xFF)) {
				if (write(sock, "+", 1) != 1) return -1;
				return n;
			}
		}
		if (write(sock, "-", 1) != 1) return -1;
	}
}

static void put_packet(const char *s) {
	char buf[PKT + 8];
	unsigned sum = 0;
	int n = 0;
	buf[n++] = '$';
	for (; *s && n < PKT; s++) {
		buf[n++] = *s;
		sum += (unsigned char)*s;
	}
	n += snprintf(buf + n, 8, "#%02x", sum & 0xFF);
	if (write(sock, buf, n) != n) return;
}

static void hex32(char *out, uint32_t v) {
	sprintf(out, "%02x%02x%02x%02x", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF);
}

static int parse_hex(const char **p, uint32_t *v) {
	int n = 0;
	*v = 0;
	while (hexval(**p) >= 0) {
		*v = *v * 16 + (uint32_t)hexval(**p);
		(*p)++;
		n++;
	}
	return n;
}

static uint32_t le_hex(const char *p) {
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) v |= (uint32_t)(hexval(p[2 * i]) * 16 + hexval(p[2 * i + 1])) << (8 * i);
	return v;
}

static int is_bp(uint32_t a) {
	for (int i = 0; i < nbps; i++)
		if (bps[i] == a) return 1;
	return 0;
}

static int interrupted(void) {
	struct pollfd pf;
	pf.fd = sock;
	pf.events = POLLIN;
	if (poll(&pf, 1, 0) > 0) {
		char c;
		if (read(sock, &c, 1) == 1 && c == 3) return 1;
	}
	return 0;
}

static void stop_reply(int sig) {
	char r[16];
	if (cpu.halted) sprintf(r, "W%02x", cpu.exit_code & 0xFF);
	else sprintf(r, "S%02x", sig);
	put_packet(r);
}

static void run_cont(void) {
	unsigned long n = 0;
	if (!cpu.halted) cpu_step();
	while (!cpu.halted) {
		if (is_bp(cpu.PC)) break;
		if ((++n & 0xFFFF) == 0 && interrupted()) {
			stop_reply(2);
			return;
		}
		cpu_step();
	}
	stop_reply(5);
}

static void handle(const char *p) {
	char out[PKT];
	uint32_t a, l, v;
	switch (p[0]) {
	case '?':
		stop_reply(5);
		return;
	case 'g': {
		int k = 0;
		for (int i = 0; i < 8; i++) hex32(out + 8 * k++, cpu.registers[i]);
		hex32(out + 8 * k++, cpu.PC);
		hex32(out + 8 * k++, cpu.SP);
		hex32(out + 8 * k++, cpu_get_flags());
		put_packet(out);
		return;
	}
	case 'G':
		if (strlen(p + 1) < 88) {
			put_packet("E01");
			return;
		}
		for (int i = 0; i < 8; i++) cpu.registers[i] = le_hex(p + 1 + 8 * i);
		cpu.PC = le_hex(p + 65);
		cpu.SP = le_hex(p + 73);
		cpu_set_flags(le_hex(p + 81));
		cpu_flush_tlb();
		put_packet("OK");
		return;
	case 'm':
		p++;
		if (!parse_hex(&p, &a) || *p++ != ',' || !parse_hex(&p, &l) || l > PKT / 2 - 1) {
			put_packet("E01");
			return;
		}
		for (uint32_t i = 0; i < l; i++) {
			uint8_t b;
			if (!mem_peek(a + i, &b)) {
				put_packet("E01");
				return;
			}
			sprintf(out + 2 * i, "%02x", b);
		}
		out[2 * l] = 0;
		put_packet(out);
		return;
	case 'M':
		p++;
		if (!parse_hex(&p, &a) || *p++ != ',' || !parse_hex(&p, &l) || *p++ != ':' || strlen(p) < 2 * l) {
			put_packet("E01");
			return;
		}
		for (uint32_t i = 0; i < l; i++) {
			int h = hexval(p[2 * i]), lo = hexval(p[2 * i + 1]);
			if (h < 0 || lo < 0 || !mem_poke(a + i, (uint8_t)(h * 16 + lo))) {
				put_packet("E01");
				return;
			}
		}
		put_packet("OK");
		return;
	case 'c':
		p++;
		if (parse_hex(&p, &a)) cpu.PC = a;
		run_cont();
		return;
	case 's':
		p++;
		if (parse_hex(&p, &a)) cpu.PC = a;
		if (!cpu.halted) cpu_step();
		stop_reply(5);
		return;
	case 'Z':
	case 'z':
		if (p[1] != '0' || p[2] != ',') {
			put_packet("");
			return;
		}
		p += 3;
		if (!parse_hex(&p, &a)) {
			put_packet("E01");
			return;
		}
		if (*p == ',') {
			p++;
			parse_hex(&p, &v);
		}
		if (*p) {
			put_packet("E01");
			return;
		}
		if (pkt[0] == 'Z') {
			if (!is_bp(a)) {
				if (nbps == MAXBP) {
					put_packet("E01");
					return;
				}
				bps[nbps++] = a;
			}
		} else {
			for (int i = 0; i < nbps; i++)
				if (bps[i] == a) bps[i--] = bps[--nbps];
		}
		put_packet("OK");
		return;
	case 'q':
		if (strncmp(p, "qSupported", 10) == 0) put_packet("PacketSize=1000");
		else if (strcmp(p, "qAttached") == 0) put_packet("1");
		else put_packet("");
		return;
	case 'H':
		put_packet("OK");
		return;
	}
	put_packet("");
}

int gdb_serve(int port) {
	struct sockaddr_in sa;
	int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
	if (ls < 0) {
		perror("socket");
		return EXIT_FATAL;
	}
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(ls, (struct sockaddr *)&sa, sizeof(sa)) || listen(ls, 1)) {
		perror("gdb stub");
		close(ls);
		return EXIT_FATAL;
	}
	fprintf(stderr, "gdb stub listening on port %d\n", port);
	sock = accept(ls, NULL, NULL);
	close(ls);
	if (sock < 0) {
		perror("accept");
		return EXIT_FATAL;
	}
	for (;;) {
		int n = get_packet();
		if (n < 0) break;
		if (pkt[0] == 'k') break;
		if (pkt[0] == 'D') {
			put_packet("OK");
			close(sock);
			sock = -1;
			cpu_run();
			return 0;
		}
		handle(pkt);
		if (cpu.halted && (pkt[0] == 'c' || pkt[0] == 's')) break;
	}
	if (sock >= 0) close(sock);
	sock = -1;
	if (!cpu.halted) cpu_stop(0);
	return 0;
}
