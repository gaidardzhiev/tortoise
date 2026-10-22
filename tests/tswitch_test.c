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
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int fails, checks, port;

#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int endpoint(void) {
	int s = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bind(s, (struct sockaddr *)&a, sizeof(a));
	return s;
}

static void send_frame(int s, int dst, int src, int tag) {
	unsigned char f[60];
	struct sockaddr_in to;
	memset(f, 0, sizeof(f));
	if (dst < 0) memset(f, 0xFF, 6);
	else {
		f[0] = 2;
		f[5] = (unsigned char)dst;
	}
	f[6] = 2;
	f[11] = (unsigned char)src;
	f[12] = 0x88;
	f[13] = 0xB5;
	f[14] = (unsigned char)tag;
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	to.sin_port = htons((unsigned short)port);
	sendto(s, f, sizeof(f), 0, (struct sockaddr *)&to, sizeof(to));
	usleep(30000);
}

static int got(int s, int tag) {
	unsigned char f[2048];
	int seen = 0;
	ssize_t n;
	while ((n = recv(s, f, sizeof(f), MSG_DONTWAIT)) > 0)
		if (n >= 15 && f[14] == tag) seen++;
	return seen;
}

int main(void) {
	int a, b, c, pid;
	char ps[16];
	port = 46000 + getpid() % 1000;
	snprintf(ps, sizeof(ps), "%d", port);
	pid = fork();
	if (pid == 0) {
		execl("./tswitch", "tswitch", ps, (char *)NULL);
		_exit(127);
	}
	usleep(200000);
	a = endpoint();
	b = endpoint();
	c = endpoint();
	send_frame(a, -1, 1, 10);
	CHECK(got(b, 10) == 0 && got(c, 10) == 0);
	send_frame(b, -1, 2, 11);
	CHECK(got(a, 11) == 1 && got(b, 11) == 0 && got(c, 11) == 0);
	send_frame(c, 9, 3, 12);
	CHECK(got(a, 12) == 1 && got(b, 12) == 1 && got(c, 12) == 0);
	send_frame(a, 2, 1, 13);
	CHECK(got(b, 13) == 1 && got(c, 13) == 0 && got(a, 13) == 0);
	send_frame(b, 3, 2, 14);
	CHECK(got(c, 14) == 1 && got(a, 14) == 0);
	send_frame(c, 1, 3, 15);
	CHECK(got(a, 15) == 1 && got(b, 15) == 0);
	send_frame(c, -1, 2, 16);
	CHECK(got(a, 16) == 1 && got(b, 16) == 1);
	send_frame(a, 2, 1, 17);
	CHECK(got(c, 17) == 1 && got(b, 17) == 0);
	send_frame(a, 1, 1, 18);
	CHECK(got(a, 18) == 0 && got(b, 18) == 0 && got(c, 18) == 0);
	kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
	printf("tswitch_test: %d checks, %d failures\n", checks, fails);
	return fails != 0;
}
