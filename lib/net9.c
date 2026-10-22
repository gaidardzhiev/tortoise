/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <net9.h>

static int translate(const char *addr, char *clone, char *dest) {
	char ans[128], *sp;
	int fd = open("/net/cs", O_RDWR), n;
	if (fd < 0) return -1;
	if (write(fd, addr, strlen(addr)) < 0) {
		close(fd);
		return -1;
	}
	lseek(fd, 0, SEEK_SET);
	n = read(fd, ans, sizeof(ans) - 1);
	close(fd);
	if (n <= 0) return -1;
	ans[n] = 0;
	if ((sp = strchr(ans, '\n')) != NULL) *sp = 0;
	sp = strchr(ans, ' ');
	if (!sp) return -1;
	*sp++ = 0;
	strcpy(clone, ans);
	strcpy(dest, sp);
	return 0;
}

static int conv_dir(int cfd, const char *clone, char *dir) {
	char num[16];
	const char *slash = strrchr(clone, '/');
	int n = read(cfd, num, sizeof(num) - 1), len;
	if (n <= 0 || !slash) return -1;
	num[n] = 0;
	len = (int)(slash - clone);
	snprintf(dir, NETPATHLEN, "%.*s/%s", len, clone, num);
	return 0;
}

static int ctl_write(int cfd, const char *fmt, const char *a, const char *b) {
	char msg[128];
	snprintf(msg, sizeof(msg), fmt, a, b);
	return write(cfd, msg, strlen(msg)) < 0 ? -1 : 0;
}

int dial(const char *addr, const char *local, char *dir, int *cfdp) {
	char clone[64], dest[64], cdir[NETPATHLEN], data[NETPATHLEN + 8];
	int cfd, dfd;
	if (translate(addr, clone, dest) < 0) return -1;
	cfd = open(clone, O_RDWR);
	if (cfd < 0) return -1;
	if (conv_dir(cfd, clone, cdir) < 0 || ctl_write(cfd, local ? "connect %s!%s" : "connect %s", dest, local) < 0) {
		close(cfd);
		return -1;
	}
	snprintf(data, sizeof(data), "%s/data", cdir);
	dfd = open(data, O_RDWR);
	if (dfd < 0) {
		close(cfd);
		return -1;
	}
	if (dir) strcpy(dir, cdir);
	if (cfdp) *cfdp = cfd;
	else close(cfd);
	return dfd;
}

int announce(const char *addr, char *dir) {
	char clone[64], dest[64], cdir[NETPATHLEN];
	int cfd;
	if (translate(addr, clone, dest) < 0) return -1;
	cfd = open(clone, O_RDWR);
	if (cfd < 0) return -1;
	if (conv_dir(cfd, clone, cdir) < 0 || ctl_write(cfd, "announce %s", dest, NULL) < 0) {
		close(cfd);
		return -1;
	}
	if (dir) strcpy(dir, cdir);
	return cfd;
}

int listen9(const char *dir, char *newdir) {
	char path[NETPATHLEN + 8], num[16];
	const char *slash = strrchr(dir, '/');
	int cfd, n;
	snprintf(path, sizeof(path), "%s/listen", dir);
	cfd = open(path, O_RDWR);
	if (cfd < 0) return -1;
	n = read(cfd, num, sizeof(num) - 1);
	if (n <= 0 || !slash) {
		close(cfd);
		return -1;
	}
	num[n] = 0;
	if (newdir) snprintf(newdir, NETPATHLEN, "%.*s/%s", (int)(slash - dir), dir, num);
	return cfd;
}

int accept9(int ctl, const char *dir) {
	char path[NETPATHLEN + 8];
	(void)ctl;
	snprintf(path, sizeof(path), "%s/data", dir);
	return open(path, O_RDWR);
}

int reject(int ctl, const char *dir, const char *cause) {
	(void)dir;
	(void)cause;
	return hangup(ctl);
}

int hangup(int ctl) {
	return write(ctl, "hangup", 6) < 0 ? -1 : 0;
}

char *netmkaddr(const char *linear, const char *defnet, const char *defsrv) {
	static char buf[128];
	const char *p = strchr(linear, '!');
	if (!p) {
		if (defsrv) snprintf(buf, sizeof(buf), "%s!%s!%s", defnet ? defnet : "net", linear, defsrv);
		else snprintf(buf, sizeof(buf), "%s!%s", defnet ? defnet : "net", linear);
	} else if (!strchr(p + 1, '!') && defsrv) snprintf(buf, sizeof(buf), "%s!%s", linear, defsrv);
	else snprintf(buf, sizeof(buf), "%s", linear);
	return buf;
}
