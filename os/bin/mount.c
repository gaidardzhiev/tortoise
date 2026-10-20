#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int main(int argc, char **argv) {
	if (argc == 1) {
		char buf[512];
		int fd = open("/proc/mounts", O_RDONLY), n;
		if (fd < 0) {
			printf("usage: mount source target type\n");
			return 1;
		}
		while ((n = read(fd, buf, sizeof(buf))) > 0) write(1, buf, n);
		close(fd);
		return 0;
	}
	if (argc != 4) {
		printf("usage: mount source target type\n");
		return 1;
	}
	if (mount(argv[1], argv[2], argv[3]) < 0) {
		printf("mount: cannot mount %s on %s: error %d\n", argv[1], argv[2], errno);
		return 1;
	}
	return 0;
}
