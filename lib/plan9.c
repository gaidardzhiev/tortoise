/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <errno.h>
#include <string.h>
#include <plan9.h>

int __syscall(int n, int a, int b, int c, int d, int e);

static int ret(int r) {
	if (r < 0 && r > -4096) {
		errno = -r;
		return -1;
	}
	return r;
}

int rfork(int flags) { return ret(__syscall(100, flags, 0, 0, 0, 0)); }
int bind9(const char *new, const char *old, int flags) { return ret(__syscall(101, (int)new, (int)old, flags, 0, 0)); }
int mount9(int fd, int afd, const char *old, int flags, const char *aname) { return ret(__syscall(102, fd, afd, (int)old, flags, (int)aname)); }
int unmount(const char *new, const char *old) { return ret(__syscall(103, (int)new, (int)old, 0, 0, 0)); }
int fd2path(int fd, char *buf, int n) { return ret(__syscall(104, fd, (int)buf, n, 0, 0)); }

int errstr(char *buf, unsigned n) {
	int saved = errno, r = ret(__syscall(105, (int)buf, (int)n, 0, 0, 0));
	if (r == 0 && n > 0 && buf[0] == 0 && saved) {
		strncpy(buf, strerror(saved), n - 1);
		buf[n - 1] = 0;
	}
	errno = saved;
	return r;
}

int notify(void (*f)(void *ureg, char *msg)) { return ret(__syscall(106, (int)f, 0, 0, 0, 0)); }
int noted(int how) { return ret(__syscall(107, how, 0, 0, 0, 0)); }
