#include "kernel.h"

int sock_read(File *f, char *buf, unsigned n) {
	(void)f;
	(void)buf;
	(void)n;
	return -EBADF;
}

int sock_write(File *f, const char *buf, unsigned n) {
	(void)f;
	(void)buf;
	(void)n;
	return -EBADF;
}

int sock_poll(File *f) {
	(void)f;
	return POLLNVAL;
}

int sock_ioctl(File *f, unsigned req, unsigned arg) {
	(void)f;
	(void)req;
	(void)arg;
	return -EINVAL;
}

void sock_close(File *f) {
	(void)f;
}
