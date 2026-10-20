#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static void count(int fd, const char *name) {
	char buf[512];
	int n, i, inword = 0, l = 0, w = 0, c = 0;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (i = 0; i < n; i++) {
			int ch = buf[i];
			c++;
			if (ch == '\n') l++;
			if (ch == ' ' || ch == '\t' || ch == '\n') inword = 0;
			else if (!inword) {
				inword = 1;
				w++;
			}
		}
	}
	if (name) printf("%d %d %d %s\n", l, w, c, name);
	else printf("%d %d %d\n", l, w, c);
}

int main(int argc, char **argv) {
	int i;
	if (argc < 2) {
		count(0, NULL);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd < 0) {
			printf("wc: cannot open %s\n", argv[i]);
			continue;
		}
		count(fd, argv[i]);
		close(fd);
	}
	return 0;
}
