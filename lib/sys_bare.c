/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define UART_DATA ((volatile unsigned *)0xF0002000)
#define UART_STATUS ((volatile unsigned *)0xF0002004)
#define POWEROFF ((volatile unsigned *)0xF0007000)

extern char _end[];
int errno;
static char *brk_ptr;

int write(int fd, const void *buf, int n) {
	const char *p = buf;
	int i;
	if (fd != 1 && fd != 2) return -1;
	for (i = 0; i < n; i++) *UART_DATA = (unsigned char)p[i];
	return n;
}

int read(int fd, void *buf, int n) {
	char *p = buf;
	int i = 0;
	if (fd != 0) return -1;
	while (i < n) {
		int c;
		while (!(*UART_STATUS & 1)) ;
		c = (int)*UART_DATA;
		p[i++] = (char)c;
		if (c == '\n') break;
	}
	return i;
}

void _exit(int status) {
	*POWEROFF = (unsigned)status;
	for (;;) ;
}

void *sbrk(int incr) {
	char here;
	char *old;
	if (!brk_ptr) brk_ptr = (char *)(((unsigned)_end + 7) & ~7u);
	old = brk_ptr;
	if (incr > 0 && (unsigned)(brk_ptr + incr) > (unsigned)&here - 65536) {
		errno = ENOMEM;
		return (void *)-1;
	}
	brk_ptr += incr;
	return old;
}

int open(const char *path, int flags, ...) {
	(void)path;
	(void)flags;
	errno = ENOENT;
	return -1;
}

int close(int fd) {
	(void)fd;
	return -1;
}

int unlink(const char *path) {
	(void)path;
	return -1;
}

int getpid(void) {
	return 1;
}

int time(void) {
	return (int)*(volatile unsigned *)0xF0005000;
}
