/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int fails;

static void expect(const char *what, int ok) {
	if (ok) printf("ok   %s\n", what);
	else {
		printf("FAIL %s (errno %d)\n", what, errno);
		fails++;
	}
	fflush(stdout);
}

static struct sockaddr_un un(const char *path) {
	struct sockaddr_un a;
	memset(&a, 0, sizeof(a));
	a.sun_family = AF_UNIX;
	strcpy(a.sun_path, path);
	return a;
}

static struct sockaddr_in in4(const char *addr, int port) {
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)port);
	a.sin_addr.s_addr = inet_addr(addr);
	return a;
}

static int readall(int fd, char *buf, int n) {
	int got = 0, r;
	while (got < n && (r = read(fd, buf + got, n - got)) > 0) got += r;
	return got;
}

static void unix_tests(void) {
	struct sockaddr_un a = un("/tmp/stream.sock"), b, from;
	struct stat st;
	socklen_t len;
	char buf[128];
	int l, c, s, d1, d2, sp[2], r;
	struct pollfd pf;
	l = socket(AF_UNIX, SOCK_STREAM, 0);
	expect("unix stream socket", l >= 0);
	expect("bind creates a socket file", bind(l, (struct sockaddr *)&a, sizeof(a)) == 0 && stat("/tmp/stream.sock", &st) == 0 && S_ISSOCK(st.st_mode));
	expect("bind to an existing path: EADDRINUSE", bind(socket(AF_UNIX, SOCK_STREAM, 0), (struct sockaddr *)&a, sizeof(a)) == -1 && errno == EADDRINUSE);
	expect("listen", listen(l, 4) == 0);
	fcntl(l, F_SETFL, O_NONBLOCK);
	expect("non blocking accept: EAGAIN", accept(l, NULL, NULL) == -1 && errno == EAGAIN);
	fcntl(l, F_SETFL, 0);
	c = socket(AF_UNIX, SOCK_STREAM, 0);
	expect("connect", connect(c, (struct sockaddr *)&a, sizeof(a)) == 0);
	pf.fd = l;
	pf.events = POLLIN;
	expect("poll sees the pending connection", poll(&pf, 1, 0) == 1 && (pf.revents & POLLIN));
	s = accept(l, NULL, NULL);
	expect("accept", s >= 0);
	expect("write to the server", write(c, "ping", 4) == 4);
	expect("server reads it", read(s, buf, sizeof(buf)) == 4 && !memcmp(buf, "ping", 4));
	expect("reply", write(s, "pong!", 5) == 5 && read(c, buf, sizeof(buf)) == 5 && !memcmp(buf, "pong!", 5));
	expect("MSG_PEEK leaves the data", send(s, "xy", 2, 0) == 2 && recv(c, buf, 2, MSG_PEEK) == 2 && recv(c, buf + 2, 2, 0) == 2 && !memcmp(buf + 2, "xy", 2));
	len = sizeof(b);
	expect("getpeername gives the path", getpeername(c, (struct sockaddr *)&b, &len) == 0 && !strcmp(b.sun_path, "/tmp/stream.sock"));
	shutdown(c, SHUT_WR);
	expect("shutdown SHUT_WR gives the server end of file", read(s, buf, sizeof(buf)) == 0);
	expect("the other direction still works", write(s, "late", 4) == 4 && read(c, buf, 4) == 4);
	close(c);
	signal(SIGPIPE, SIG_IGN);
	expect("write after the peer closed: EPIPE", write(s, "x", 1) == -1 && errno == EPIPE);
	close(s);
	close(l);
	c = socket(AF_UNIX, SOCK_STREAM, 0);
	expect("connect to a closed listener: ECONNREFUSED", connect(c, (struct sockaddr *)&a, sizeof(a)) == -1 && errno == ECONNREFUSED);
	a = un("/tmp/nothing.sock");
	expect("connect to a missing path: ECONNREFUSED", connect(c, (struct sockaddr *)&a, sizeof(a)) == -1 && errno == ECONNREFUSED);
	close(c);

	d1 = socket(AF_UNIX, SOCK_DGRAM, 0);
	d2 = socket(AF_UNIX, SOCK_DGRAM, 0);
	a = un("/tmp/d1.sock");
	b = un("/tmp/d2.sock");
	bind(d1, (struct sockaddr *)&a, sizeof(a));
	bind(d2, (struct sockaddr *)&b, sizeof(b));
	expect("unix datagram sendto", sendto(d2, "dgram one", 9, 0, (struct sockaddr *)&a, sizeof(a)) == 9);
	sendto(d2, "two", 3, 0, (struct sockaddr *)&a, sizeof(a));
	len = sizeof(from);
	r = recvfrom(d1, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);
	expect("datagrams keep their boundaries and sender", r == 9 && !memcmp(buf, "dgram one", 9) && !strcmp(from.sun_path, "/tmp/d2.sock"));
	expect("second datagram", recv(d1, buf, sizeof(buf), 0) == 3);
	expect("connected datagram socket", connect(d1, (struct sockaddr *)&b, sizeof(b)) == 0 && send(d1, "back", 4, 0) == 4 && recv(d2, buf, sizeof(buf), 0) == 4);
	close(d1);
	close(d2);

	expect("socketpair", socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
	expect("socketpair carries data both ways", write(sp[0], "abc", 3) == 3 && read(sp[1], buf, 3) == 3 && write(sp[1], "d", 1) == 1 && read(sp[0], buf, 1) == 1 && buf[0] == 'd');
	close(sp[0]);
	expect("socketpair end of file", read(sp[1], buf, 1) == 0);
	close(sp[1]);
}

static void udp_tests(void) {
	struct sockaddr_in a = in4("127.0.0.1", 5000), from, nobody = in4("127.0.0.1", 5999);
	socklen_t len = sizeof(from);
	char buf[64];
	int u = socket(AF_INET, SOCK_DGRAM, 0), v = socket(AF_INET, SOCK_DGRAM, 0), r, t;
	expect("udp bind", bind(u, (struct sockaddr *)&a, sizeof(a)) == 0);
	expect("udp bind of a used port: EADDRINUSE", bind(socket(AF_INET, SOCK_DGRAM, 0), (struct sockaddr *)&a, sizeof(a)) == -1 && errno == EADDRINUSE);
	expect("udp sendto over lo", sendto(v, "hello udp", 9, 0, (struct sockaddr *)&a, sizeof(a)) == 9);
	r = recvfrom(u, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);
	expect("udp recvfrom", r == 9 && !memcmp(buf, "hello udp", 9) && from.sin_addr.s_addr == htonl(INADDR_LOOPBACK) && ntohs(from.sin_port) >= 49152);
	expect("reply to the sender's port", sendto(u, "reply", 5, 0, (struct sockaddr *)&from, len) == 5 && recv(v, buf, sizeof(buf), 0) == 5);
	expect("connect a udp socket to a closed port", connect(v, (struct sockaddr *)&nobody, sizeof(nobody)) == 0);
	send(v, "x", 1, 0);
	expect("port unreachable gives ECONNREFUSED", recv(v, buf, sizeof(buf), 0) == -1 && errno == ECONNREFUSED);
	t = 20;
	setsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
	expect("SO_RCVTIMEO times out with EAGAIN", recv(u, buf, sizeof(buf), 0) == -1 && errno == EAGAIN);
	close(u);
	close(v);
}

static void tcp_tests(void) {
	struct sockaddr_in a = in4("127.0.0.1", 7000), b, closed = in4("127.0.0.1", 7999);
	socklen_t len;
	char *big, buf[256];
	int l, c, s, pid, st, i, one = 1, fd, n, sum1 = 0, sum2 = 0;
	l = socket(AF_INET, SOCK_STREAM, 0);
	expect("tcp socket and bind", l >= 0 && bind(l, (struct sockaddr *)&a, sizeof(a)) == 0);
	expect("tcp listen", listen(l, 4) == 0);
	c = socket(AF_INET, SOCK_STREAM, 0);
	expect("tcp connect over lo", connect(c, (struct sockaddr *)&a, sizeof(a)) == 0);
	len = sizeof(b);
	s = accept(l, (struct sockaddr *)&b, &len);
	expect("tcp accept gives the client's address", s >= 0 && b.sin_addr.s_addr == htonl(INADDR_LOOPBACK) && ntohs(b.sin_port) >= 49152);
	len = sizeof(b);
	expect("getsockname", getsockname(s, (struct sockaddr *)&b, &len) == 0 && ntohs(b.sin_port) == 7000);
	expect("data client to server", write(c, "GET /", 5) == 5 && read(s, buf, sizeof(buf)) == 5);
	expect("data server to client", write(s, "200 OK", 6) == 6 && read(c, buf, 6) == 6 && !memcmp(buf, "200 OK", 6));
	fd = open("/proc/net/tcp", O_RDONLY);
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	buf[n > 0 ? n : 0] = 0;
	expect("/proc/net/tcp shows LISTEN and ESTABLISHED", strstr(buf, "127.0.0.1:7000 0.0.0.0:0 LISTEN") != NULL && strstr(buf, "ESTABLISHED") != NULL);
	big = malloc(100000);
	for (i = 0; i < 100000; i++) {
		big[i] = (char)(i * 31 + 7);
		sum1 += big[i];
	}
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		int w = 0, r;
		while (w < 100000 && (r = write(c, big + w, 100000 - w > 3000 ? 3000 : 100000 - w)) > 0) w += r;
		shutdown(c, SHUT_WR);
		_exit(w == 100000 ? 0 : 1);
	}
	memset(big, 0, 100000);
	n = readall(s, big, 100000);
	for (i = 0; i < n; i++) sum2 += big[i];
	waitpid(pid, &st, 0);
	expect("100000 bytes through 8KB windows arrive intact", n == 100000 && sum1 == sum2 && WEXITSTATUS(st) == 0);
	expect("shutdown SHUT_WR gives end of file", read(s, buf, 1) == 0);
	close(c);
	close(s);
	c = socket(AF_INET, SOCK_STREAM, 0);
	expect("connect to a closed port: ECONNREFUSED", connect(c, (struct sockaddr *)&closed, sizeof(closed)) == -1 && errno == ECONNREFUSED);
	close(c);
	c = socket(AF_INET, SOCK_STREAM, 0);
	expect("bind to a listening port: EADDRINUSE", bind(c, (struct sockaddr *)&a, sizeof(a)) == -1 && errno == EADDRINUSE);
	close(c);
	c = socket(AF_INET, SOCK_STREAM, 0);
	connect(c, (struct sockaddr *)&a, sizeof(a));
	s = accept(l, NULL, NULL);
	close(s);
	usleep(20000);
	signal(SIGPIPE, SIG_IGN);
	write(c, "x", 1);
	usleep(20000);
	expect("write after the peer reset: EPIPE", write(c, "y", 1) == -1 && (errno == EPIPE || errno == ECONNRESET));
	close(c);
	close(l);
	l = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	expect("SO_REUSEADDR binds again after close", bind(l, (struct sockaddr *)&a, sizeof(a)) == 0);
	close(l);
	free(big);
}

static void raw_tests(void) {
	struct sockaddr_in a = in4("127.0.0.1", 0), from;
	socklen_t len = sizeof(from);
	unsigned char pkt[64], reply[128];
	unsigned sum = 0;
	int r, i, s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	expect("raw ICMP socket as root", s >= 0);
	memset(pkt, 0, sizeof(pkt));
	pkt[0] = 8;
	pkt[4] = 0x12;
	pkt[5] = 0x34;
	pkt[7] = 1;
	for (i = 8; i < 40; i++) pkt[i] = (unsigned char)i;
	for (i = 0; i < 40; i += 2) sum += (unsigned)(pkt[i] << 8 | pkt[i + 1]);
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
	sum = ~sum & 0xFFFF;
	pkt[2] = (unsigned char)(sum >> 8);
	pkt[3] = (unsigned char)sum;
	expect("send an echo request", sendto(s, pkt, 40, 0, (struct sockaddr *)&a, sizeof(a)) == 40);
	for (i = 0; i < 4; i++) {
		r = recvfrom(s, reply, sizeof(reply), 0, (struct sockaddr *)&from, &len);
		if (r >= 28 && reply[20] == 0) break;
	}
	expect("echo reply with the same id and sequence", r == 60 && reply[20] == 0 && reply[24] == 0x12 && reply[25] == 0x34 && reply[27] == 1 && !memcmp(reply + 28, pkt + 8, 32));
	close(s);
}

static int as_user(void) {
	struct sockaddr_in a = in4("127.0.0.1", 80);
	int s;
	setgid(100);
	setuid(1000);
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) == 0 || errno != EACCES) return 1;
	if (socket(AF_INET, SOCK_RAW, IPPROTO_ICMP) >= 0 || errno != EPERM) return 2;
	return 0;
}

int main(void) {
	int pid, st;
	struct hostent *h;
	struct servent *sv;
	unix_tests();
	udp_tests();
	tcp_tests();
	raw_tests();
	fflush(stdout);
	pid = fork();
	if (pid == 0) _exit(as_user());
	waitpid(pid, &st, 0);
	expect("ports below 1024 and raw sockets need root", WEXITSTATUS(st) == 0);
	h = gethostbyname("localhost");
	expect("gethostbyname from /etc/hosts", h && ((struct in_addr *)h->h_addr)->s_addr == htonl(INADDR_LOOPBACK));
	expect("gethostbyname of a dotted address", (h = gethostbyname("10.1.2.3")) && ((struct in_addr *)h->h_addr)->s_addr == inet_addr("10.1.2.3"));
	expect("gethostbyname of an unknown name", gethostbyname("nowhere") == NULL);
	sv = getservbyname("http", "tcp");
	expect("getservbyname", sv && ntohs((unsigned short)sv->s_port) == 80);
	expect("inet_ntoa and inet_addr", !strcmp(inet_ntoa(*(struct in_addr *)&(in4("192.168.1.20", 0).sin_addr)), "192.168.1.20") && inet_addr("1.2.3.256") == INADDR_NONE);
	printf("%s\n", fails ? "socktest: FAILED" : "socktest: all ok");
	return fails != 0;
}
