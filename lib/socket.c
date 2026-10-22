/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int __syscall(int n, int a, int b, int c, int d, int e);

static int ret(int r) {
	if (r < 0 && r > -4096) {
		errno = -r;
		return -1;
	}
	return r;
}

int socket(int domain, int type, int proto) { return ret(__syscall(70, domain, type, proto, 0, 0)); }
int bind(int fd, const struct sockaddr *addr, socklen_t len) { (void)len; return ret(__syscall(71, fd, (int)addr, 0, 0, 0)); }
int listen(int fd, int backlog) { return ret(__syscall(72, fd, backlog, 0, 0, 0)); }
int connect(int fd, const struct sockaddr *addr, socklen_t len) { (void)len; return ret(__syscall(74, fd, (int)addr, 0, 0, 0)); }

static unsigned addr_len(const struct sockaddr *a) {
	return a->sa_family == AF_UNIX ? 110 : 16;
}

int accept(int fd, struct sockaddr *addr, socklen_t *len) {
	char tmp[112];
	int r = ret(__syscall(73, fd, addr ? (int)tmp : 0, 0, 0, 0));
	if (r >= 0 && addr && len) {
		unsigned l = addr_len((struct sockaddr *)tmp);
		memcpy(addr, tmp, l < *len ? l : *len);
		*len = l;
	}
	return r;
}

int sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *addr, socklen_t len) {
	(void)len;
	return ret(__syscall(75, fd, (int)buf, (int)n, flags, (int)addr));
}

int send(int fd, const void *buf, size_t n, int flags) {
	return sendto(fd, buf, n, flags, NULL, 0);
}

int recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *addr, socklen_t *len) {
	char tmp[112];
	int r = ret(__syscall(76, fd, (int)buf, (int)n, flags, addr ? (int)tmp : 0));
	if (r >= 0 && addr && len) {
		unsigned l = addr_len((struct sockaddr *)tmp);
		memcpy(addr, tmp, l < *len ? l : *len);
		*len = l;
	}
	return r;
}

int recv(int fd, void *buf, size_t n, int flags) {
	return recvfrom(fd, buf, n, flags, NULL, NULL);
}

int shutdown(int fd, int how) { return ret(__syscall(77, fd, how, 0, 0, 0)); }
int setsockopt(int fd, int level, int name, const void *val, socklen_t len) { return ret(__syscall(78, fd, level, name, (int)val, (int)len)); }

static int name_call(int nr, int fd, struct sockaddr *addr, socklen_t *len) {
	char tmp[112];
	int r = ret(__syscall(nr, fd, (int)tmp, 0, 0, 0));
	if (r >= 0 && addr && len) {
		unsigned l = addr_len((struct sockaddr *)tmp);
		memcpy(addr, tmp, l < *len ? l : *len);
		*len = l;
	}
	return r;
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *len) { return name_call(79, fd, addr, len); }
int getpeername(int fd, struct sockaddr *addr, socklen_t *len) { return name_call(80, fd, addr, len); }
int socketpair(int domain, int type, int proto, int fds[2]) { return ret(__syscall(81, domain, type, proto, (int)fds, 0)); }

unsigned short htons(unsigned short v) { return (unsigned short)((v >> 8) | (v << 8)); }
unsigned short ntohs(unsigned short v) { return htons(v); }
unsigned htonl(unsigned v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }
unsigned ntohl(unsigned v) { return htonl(v); }

int inet_aton(const char *s, struct in_addr *a) {
	unsigned p[4], v = 0;
	int i;
	for (i = 0; i < 4; i++) {
		char *end;
		if (*s < '0' || *s > '9') return 0;
		p[i] = (unsigned)strtoul(s, &end, 10);
		if (p[i] > 255) return 0;
		s = end;
		if (i < 3) {
			if (*s != '.') return 0;
			s++;
		}
	}
	if (*s) return 0;
	for (i = 0; i < 4; i++) v = v << 8 | p[i];
	a->s_addr = htonl(v);
	return 1;
}

in_addr_t inet_addr(const char *s) {
	struct in_addr a;
	return inet_aton(s, &a) ? a.s_addr : INADDR_NONE;
}

char *inet_ntoa(struct in_addr a) {
	static char buf[16];
	unsigned v = ntohl(a.s_addr);
	snprintf(buf, sizeof(buf), "%u.%u.%u.%u", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255);
	return buf;
}

static char *words(char *line, char **w, int max) {
	int n = 0;
	char *p = line;
	while (*p && n < max) {
		while (*p == ' ' || *p == '\t') p++;
		if (!*p || *p == '\n' || *p == '#') break;
		w[n++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
		if (*p) *p++ = 0;
	}
	w[n] = NULL;
	return n ? w[0] : NULL;
}

struct hostent *gethostbyname(const char *name) {
	static struct hostent h;
	static struct in_addr a;
	static char *addrs[2], hname[64], *noalias[1];
	char line[256], *w[9];
	FILE *f;
	if (inet_aton(name, &a)) {
		strncpy(hname, name, sizeof(hname) - 1);
	} else {
		int found = 0;
		f = fopen("/etc/hosts", "r");
		if (!f) {
			errno = ENOENT;
			return NULL;
		}
		while (!found && fgets(line, sizeof(line), f)) {
			int i;
			if (!words(line, w, 8) || !w[1]) continue;
			for (i = 1; w[i]; i++)
				if (!strcmp(w[i], name) && inet_aton(w[0], &a)) {
					strncpy(hname, w[1], sizeof(hname) - 1);
					found = 1;
					break;
				}
		}
		fclose(f);
		if (!found) return NULL;
	}
	addrs[0] = (char *)&a;
	addrs[1] = NULL;
	noalias[0] = NULL;
	h.h_name = hname;
	h.h_aliases = noalias;
	h.h_addrtype = AF_INET;
	h.h_length = 4;
	h.h_addr_list = addrs;
	return &h;
}

struct servent *getservbyname(const char *name, const char *proto) {
	static struct servent s;
	static char sname[32], sproto[8], *noalias[1];
	char line[256], *w[9], *slash;
	FILE *f = fopen("/etc/services", "r");
	if (!f) return NULL;
	while (fgets(line, sizeof(line), f)) {
		int i, match = 0;
		if (!words(line, w, 8) || !w[1]) continue;
		slash = strchr(w[1], '/');
		if (!slash) continue;
		*slash = 0;
		if (proto && strcmp(slash + 1, proto)) continue;
		if (!strcmp(w[0], name)) match = 1;
		for (i = 2; w[i]; i++)
			if (!strcmp(w[i], name)) match = 1;
		if (!match) continue;
		strncpy(sname, w[0], sizeof(sname) - 1);
		strncpy(sproto, slash + 1, sizeof(sproto) - 1);
		noalias[0] = NULL;
		s.s_name = sname;
		s.s_aliases = noalias;
		s.s_port = htons((unsigned short)atoi(w[1]));
		s.s_proto = sproto;
		fclose(f);
		return &s;
	}
	fclose(f);
	return NULL;
}
