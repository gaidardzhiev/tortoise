#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static int copy(int fd) {
	char buf[512];
	int n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		if (write(1, buf, n) != n) return 1;
	return n < 0;
}

int main(int argc, char **argv) {
	int i, rc = 0;
	if (argc < 2) return copy(0);
	for (i = 1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			printf("cat: cannot open %s\n", argv[i]);
			rc = 1;
			continue;
		}
		rc |= copy(fd);
		close(fd);
	}
	return rc;
}
