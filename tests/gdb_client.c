/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int s;
static int fails;
static char reply[8192];

static void sendpkt(const char *p) {
	char buf[8192];
	unsigned sum = 0;
	const char *q;
	for (q = p; *q; q++) sum += (unsigned char)*q;
	snprintf(buf, sizeof(buf), "$%s#%02x", p, sum & 0xFF);
	if (write(s, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
		perror("write");
		exit(1);
	}
}

static int getc_s(void) {
	unsigned char c;
	if (read(s, &c, 1) != 1) return -1;
	return c;
}

static void recvpkt(void) {
	int c, n = 0;
	unsigned sum = 0, got;
	char cs[3];
	do {
		c = getc_s();
		if (c < 0) {
			fprintf(stderr, "gdb_client: connection closed\n");
			exit(1);
		}
	} while (c != '$');
	while ((c = getc_s()) >= 0 && c != '#') {
		reply[n++] = (char)c;
		sum += (unsigned)c;
	}
	reply[n] = 0;
	cs[0] = (char)getc_s();
	cs[1] = (char)getc_s();
	cs[2] = 0;
	got = (unsigned)strtoul(cs, NULL, 16);
	if (got != (sum & 0xFF)) {
		fprintf(stderr, "gdb_client: bad checksum\n");
		fails++;
	}
	if (write(s, "+", 1) != 1) exit(1);
}

static void expect_ack(void) {
	int c = getc_s();
	if (c != '+') {
		fprintf(stderr, "gdb_client: expected ack, got %d\n", c);
		fails++;
	}
}

static const char *ask(const char *p) {
	sendpkt(p);
	expect_ack();
	recvpkt();
	return reply;
}

static void want(const char *p, const char *r) {
	const char *got = ask(p);
	if (strcmp(got, r)) {
		fprintf(stderr, "gdb_client: '%s' -> '%s', want '%s'\n", p, got, r);
		fails++;
	}
}

static void want_prefix(const char *p, const char *r) {
	const char *got = ask(p);
	if (strncmp(got, r, strlen(r))) {
		fprintf(stderr, "gdb_client: '%s' -> '%s', want prefix '%s'\n", p, got, r);
		fails++;
	}
}

static unsigned reg(int i) {
	char b[9];
	unsigned v;
	memcpy(b, reply + 8 * i, 8);
	b[8] = 0;
	v = (unsigned)strtoul(b, NULL, 16);
	return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24);
}

int main(int argc, char **argv) {
	struct sockaddr_in sa;
	int i, ok = 0;
	char g[200];
	if (argc < 2) return 1;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)atoi(argv[1]));
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	for (i = 0; i < 100 && !ok; i++) {
		s = socket(AF_INET, SOCK_STREAM, 0);
		if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == 0) ok = 1;
		else {
			close(s);
			usleep(50000);
		}
	}
	if (!ok) {
		fprintf(stderr, "gdb_client: cannot connect\n");
		return 1;
	}
	want_prefix("qSupported:multiprocess+", "PacketSize=");
	want("?", "S05");
	ask("g");
	if (strlen(reply) != 88 || reg(8) != 0 || reg(9) != 0x100000) {
		fprintf(stderr, "gdb_client: bad initial registers '%s'\n", reply);
		fails++;
	}
	want("m0,6", "200100000000");
	want("Z0,c,1", "OK");
	want("c", "S05");
	ask("g");
	if (reg(8) != 0x0C || reg(1) != 1) {
		fprintf(stderr, "gdb_client: first stop pc=%x r1=%x\n", reg(8), reg(1));
		fails++;
	}
	want("c", "S05");
	ask("g");
	if (reg(8) != 0x0C || reg(1) != 2) {
		fprintf(stderr, "gdb_client: second stop pc=%x r1=%x\n", reg(8), reg(1));
		fails++;
	}
	want("z0,c,1", "OK");
	want("s", "S05");
	ask("g");
	if (reg(8) != 0x12) {
		fprintf(stderr, "gdb_client: step pc=%x\n", reg(8));
		fails++;
	}
	want("M200,4:78563412", "OK");
	want("m200,4", "78563412");
	want("m80000000,4", "E01");
	ask("g");
	strcpy(g, reply);
	memcpy(g + 8 * 5, "efbeadde", 8);
	{
		char cmd[260];
		snprintf(cmd, sizeof(cmd), "G%s", g);
		want(cmd, "OK");
	}
	ask("g");
	if (reg(5) != 0xDEADBEEF) {
		fprintf(stderr, "gdb_client: G did not set R5\n");
		fails++;
	}
	want("vMustReplyEmpty", "");
	want("c", "W07");
	close(s);
	if (fails) {
		fprintf(stderr, "gdb_client: %d failures\n", fails);
		return 1;
	}
	printf("gdb: ok\n");
	return 0;
}
