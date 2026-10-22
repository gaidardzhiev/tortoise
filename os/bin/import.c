/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <plan9.h>

#define PORT9 17007

static int dial_host(const char *host) {
	struct sockaddr_in a;
	struct hostent *h;
	int s;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(PORT9);
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
	int i = 1, flags = MREPLACE, s, n;
	char line[256], e[ERRMAX];
	const char *host, *path, *mtpt;
	while (i < argc && argv[i][0] == '-' && argv[i][1]) {
		char *o;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'b') flags = (flags & ~3) | MBEFORE;
			else if (*o == 'a') flags = (flags & ~3) | MAFTER;
			else if (*o == 'c') flags |= MCREATE;
			else {
				fprintf(stderr, "usage: import [-b|-a|-c] host path [mountpoint]\n");
				return 1;
			}
		}
		i++;
	}
	if (argc - i != 2 && argc - i != 3) {
		fprintf(stderr, "usage: import [-b|-a|-c] host path [mountpoint]\n");
		return 1;
	}
	host = argv[i];
	path = argv[i + 1];
	mtpt = argc - i == 3 ? argv[i + 2] : path;
	s = dial_host(host);
	if (s < 0) {
		fprintf(stderr, "import: cannot dial %s\n", host);
		return 1;
	}
	snprintf(line, sizeof(line), "%s\n", path);
	write(s, line, strlen(line));
	for (n = 0; n < (int)sizeof(line) - 1 && read(s, line + n, 1) == 1 && line[n] != '\n'; n++) ;
	line[n] = 0;
	if (strcmp(line, "OK")) {
		fprintf(stderr, "import: %s: %s\n", path, line);
		return 1;
	}
	if (mount9(s, -1, mtpt, flags, "") < 0) {
		e[0] = 0;
		errstr(e, sizeof(e));
		fprintf(stderr, "import: mount %s: %s\n", mtpt, e);
		return 1;
	}
	return 0;
}
