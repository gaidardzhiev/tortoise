#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int list(const char *dir, int lflag) {
	struct dirent d;
	struct stat st;
	char path[256];
	int fd;
	if (stat(dir, &st) < 0) {
		printf("ls: cannot access %s\n", dir);
		return 1;
	}
	if (!S_ISDIR(st.st_mode)) {
		if (lflag) printf("- %6u %s\n", st.st_size, dir);
		else printf("%s\n", dir);
		return 0;
	}
	fd = open(dir, O_RDONLY);
	if (fd < 0) {
		printf("ls: cannot open %s\n", dir);
		return 1;
	}
	while (readdir(fd, &d) > 0) {
		if (!strcmp(d.name, ".") || !strcmp(d.name, "..")) continue;
		if (lflag) {
			snprintf(path, sizeof(path), "%s/%s", dir, d.name);
			if (stat(path, &st) < 0) continue;
			printf("%c %6u %s\n", S_ISDIR(st.st_mode) ? 'd' : '-', st.st_size, d.name);
		} else {
			printf("%s\n", d.name);
		}
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv) {
	int i = 1, lflag = 0, rc = 0, any = 0;
	if (i < argc && !strcmp(argv[i], "-l")) {
		lflag = 1;
		i++;
	}
	for (; i < argc; i++) {
		rc |= list(argv[i], lflag);
		any = 1;
	}
	if (!any) rc = list(".", lflag);
	return rc;
}
