/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int dial_tcp(const char *spec) {
	char buf[128], *host, *port;
	struct sockaddr_in a;
	struct hostent *h;
	int s;
	strncpy(buf, spec, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	if (strncmp(buf, "tcp!", 4) && strncmp(buf, "net!", 4)) return -1;
	host = buf + 4;
	port = strchr(host, '!');
	if (!port) return -1;
	*port++ = 0;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)atoi(port));
	if (!inet_aton(host, &a.sin_addr)) {
		h = gethostbyname(host);
		if (!h) return -1;
		memcpy(&a.sin_addr, h->h_addr, 4);
	}
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) return -1;
	if (connect(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
		close(s);
		return -1;
	}
	return s;
}

int main(int argc, char **argv) {
	char path[64], num[12];
	int s, f;
	if (argc != 3) {
		fprintf(stderr, "usage: srv name tcp!host!port\n");
		return 1;
	}
	s = dial_tcp(argv[2]);
	if (s < 0) {
		fprintf(stderr, "srv: cannot dial %s\n", argv[2]);
		return 1;
	}
	snprintf(path, sizeof(path), "/srv/%s", argv[1]);
	f = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (f < 0) {
		fprintf(stderr, "srv: cannot create %s\n", path);
		return 1;
	}
	snprintf(num, sizeof(num), "%d", s);
	if (write(f, num, strlen(num)) < 0) {
		fprintf(stderr, "srv: cannot post %s\n", path);
		return 1;
	}
	close(f);
	return 0;
}
