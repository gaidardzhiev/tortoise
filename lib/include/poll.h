/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _POLL_H
#define _POLL_H
#define POLLIN 1
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32
struct pollfd {
	int fd;
	short events;
	short revents;
};
int poll(struct pollfd *fds, unsigned n, int timeout);
#endif
