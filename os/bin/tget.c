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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int main(int argc, char **argv) {
	char host[128], path[512], req[768], buf[1024], *p, *hdr;
	int port = 80, i = 1, s, out = 1, n, code = 0, got = 0, inbody = 0, hl = 0;
	struct sockaddr_in a;
	struct hostent *h;
	const char *url = NULL, *file = NULL;
	for (; i < argc; i++) {
		if (!strcmp(argv[i], "-o") && i + 1 < argc) file = argv[++i];
		else url = argv[i];
	}
	if (!url || strncmp(url, "http://", 7)) {
		fprintf(stderr, "usage: tget http://host[:port]/path [-o file]\n");
		return 2;
	}
	url += 7;
	p = strchr(url, '/');
	n = p ? (int)(p - url) : (int)strlen(url);
	if (n >= (int)sizeof(host)) n = sizeof(host) - 1;
	memcpy(host, url, (unsigned)n);
	host[n] = 0;
	strncpy(path, p ? p : "/", sizeof(path) - 1);
	path[sizeof(path) - 1] = 0;
	p = strchr(host, ':');
	if (p) {
		*p = 0;
		port = atoi(p + 1);
	}
	h = gethostbyname(host);
	if (!h) {
		fprintf(stderr, "tget: unknown host %s\n", host);
		return 1;
	}
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)port);
	memcpy(&a.sin_addr, h->h_addr, 4);
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
		fprintf(stderr, "tget: %s\n", strerror(errno));
		return 1;
	}
	snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: tget\r\n\r\n", path, host);
	write(s, req, strlen(req));
	hdr = malloc(8192);
	while ((n = read(s, buf, sizeof(buf))) > 0) {
		if (inbody) {
			write(out, buf, n);
			got += n;
			continue;
		}
		if (hl + n >= 8192) n = 8191 - hl;
		memcpy(hdr + hl, buf, (unsigned)n);
		hl += n;
		hdr[hl] = 0;
		p = strstr(hdr, "\r\n\r\n");
		if (p) {
			int start = (int)(p - hdr) + 4;
			sscanf(hdr, "HTTP/%*s %d", &code);
			if (code != 200) {
				char *e = strstr(hdr, "\r\n");
				if (e) *e = 0;
				fprintf(stderr, "tget: %s\n", hdr);
				return 1;
			}
			if (file) {
				out = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (out < 0) {
					fprintf(stderr, "tget: cannot create %s\n", file);
					return 1;
				}
			}
			inbody = 1;
			write(out, hdr + start, hl - start);
			got += hl - start;
		}
	}
	close(s);
	if (!inbody) {
		fprintf(stderr, "tget: no response\n");
		return 1;
	}
	if (out != 1) close(out);
	return 0;
}
