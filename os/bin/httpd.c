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
#include <signal.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>

static const char *root = "/var/www";

static const char *ctype(const char *p) {
	const char *e = strrchr(p, '.');
	if (!e) return "application/octet-stream";
	if (!strcmp(e, ".html") || !strcmp(e, ".htm")) return "text/html";
	if (!strcmp(e, ".txt") || !strcmp(e, ".c") || !strcmp(e, ".h") || !strcmp(e, ".sh")) return "text/plain";
	if (!strcmp(e, ".css")) return "text/css";
	if (!strcmp(e, ".js")) return "application/javascript";
	if (!strcmp(e, ".png")) return "image/png";
	if (!strcmp(e, ".gif")) return "image/gif";
	if (!strcmp(e, ".jpg") || !strcmp(e, ".jpeg")) return "image/jpeg";
	return "application/octet-stream";
}

static void put(int c, const char *s) {
	write(c, s, strlen(s));
}

static void status(int c, int code, const char *text, const char *type, int len, int head) {
	char h[256];
	snprintf(h, sizeof(h), "HTTP/1.0 %d %s\r\nServer: tortoise-httpd\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", code, text, type, len);
	put(c, h);
	(void)head;
}

static void error(int c, int code, const char *text, int head) {
	char body[128];
	snprintf(body, sizeof(body), "<html><body><h1>%d %s</h1></body></html>\n", code, text);
	status(c, code, text, "text/html", (int)strlen(body), head);
	if (!head) put(c, body);
}

static void listing(int c, const char *url, const char *path, int head) {
	char *body = malloc(16384), line[256];
	int n = 0;
	DIR *d = opendir(path);
	struct dirent *e;
	if (!body || !d) {
		error(c, 403, "Forbidden", head);
		free(body);
		return;
	}
	n += snprintf(body + n, 16384 - n, "<html><body><h1>Index of %s</h1><ul>\n", url);
	while ((e = readdir(d)) != NULL && n < 16000) {
		if (!strcmp(e->d_name, ".")) continue;
		snprintf(line, sizeof(line), "<li><a href=\"%s%s%s\">%s</a></li>\n", url, url[strlen(url) - 1] == '/' ? "" : "/", e->d_name, e->d_name);
		n += snprintf(body + n, 16384 - n, "%s", line);
	}
	closedir(d);
	n += snprintf(body + n, 16384 - n, "</ul></body></html>\n");
	status(c, 200, "OK", "text/html", n, head);
	if (!head) write(c, body, n);
	free(body);
}

static void serve(int c) {
	char req[1024], method[8], url[512], path[768];
	int n = 0, r, head, fd;
	struct stat st;
	while (n < (int)sizeof(req) - 1 && (r = read(c, req + n, sizeof(req) - 1 - n)) > 0) {
		n += r;
		req[n] = 0;
		if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) break;
	}
	req[n] = 0;
	if (sscanf(req, "%7s %511s", method, url) != 2) {
		error(c, 400, "Bad Request", 0);
		return;
	}
	head = !strcmp(method, "HEAD");
	if (!head && strcmp(method, "GET")) {
		error(c, 501, "Not Implemented", 0);
		return;
	}
	if (url[0] != '/' || strstr(url, "..")) {
		error(c, 403, "Forbidden", head);
		return;
	}
	snprintf(path, sizeof(path), "%s%s", root, url);
	if (stat(path, &st) < 0) {
		error(c, 404, "Not Found", head);
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		char idx[800];
		snprintf(idx, sizeof(idx), "%s/index.html", path);
		if (stat(idx, &st) == 0) strcpy(path, idx);
		else {
			listing(c, url, path, head);
			return;
		}
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		error(c, errno == ENOENT ? 404 : 403, errno == ENOENT ? "Not Found" : "Forbidden", head);
		return;
	}
	status(c, 200, "OK", ctype(path), (int)st.st_size, head);
	if (!head) {
		char buf[1024];
		while ((r = read(fd, buf, sizeof(buf))) > 0)
			if (write(c, buf, r) < 0) break;
	}
	close(fd);
}

int main(int argc, char **argv) {
	int i = 1, port = 80, s, one = 1;
	struct sockaddr_in a;
	if (i + 1 < argc && !strcmp(argv[i], "-p")) {
		port = atoi(argv[i + 1]);
		i += 2;
	}
	if (i < argc) root = argv[i];
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
	s = socket(AF_INET, SOCK_STREAM, 0);
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)port);
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 8) < 0) {
		fprintf(stderr, "httpd: port %d: %s\n", port, strerror(errno));
		return 1;
	}
	for (;;) {
		int c = accept(s, NULL, NULL);
		if (c < 0) {
			if (errno == EINTR) continue;
			fprintf(stderr, "httpd: accept: %s\n", strerror(errno));
			return 1;
		}
		serve(c);
		close(c);
	}
}
