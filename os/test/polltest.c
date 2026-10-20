#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

int main(void) {
	struct pollfd p;
	char buf[128];
	int r, n;
	p.fd = 0;
	p.events = POLLIN;
	r = poll(&p, 1, 0);
	printf("poll without waiting: %d\n", r);
	r = poll(&p, 1, 50);
	printf("poll with a timeout: %d revents %d\n", r, p.revents);
	n = read(0, buf, sizeof(buf) - 1);
	buf[n > 0 ? n : 0] = 0;
	printf("read: %s", buf);
	r = poll(&p, 1, -1);
	n = read(0, buf, sizeof(buf) - 1);
	buf[n > 0 ? n : 0] = 0;
	printf("poll forever: %d, read: %s", r, buf);
	return 0;
}
