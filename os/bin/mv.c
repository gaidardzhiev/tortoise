/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static const char *base(const char *p) {
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

static int move(const char *from, const char *to) {
	char buf[1024];
	int in, out, n;
	struct stat st;
	if (rename(from, to) == 0) return 0;
	if (errno != EXDEV) {
		fprintf(stderr, "mv: cannot move %s to %s\n", from, to);
		return 1;
	}
	if (stat(from, &st) < 0 || !S_ISREG(st.st_mode)) {
		fprintf(stderr, "mv: cannot move %s across file systems\n", from);
		return 1;
	}
	in = open(from, O_RDONLY);
	out = open(to, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
	if (in < 0 || out < 0) {
		fprintf(stderr, "mv: cannot move %s to %s\n", from, to);
		return 1;
	}
	while ((n = read(in, buf, sizeof(buf))) > 0) write(out, buf, n);
	close(in);
	close(out);
	return unlink(from) < 0;
}

int main(int argc, char **argv) {
	int i, rc = 0, isdir;
	struct stat st;
	char path[512];
	if (argc < 3) {
		fprintf(stderr, "usage: mv from ... to\n");
		return 1;
	}
	isdir = stat(argv[argc - 1], &st) == 0 && S_ISDIR(st.st_mode);
	if (argc > 3 && !isdir) {
		fprintf(stderr, "mv: %s is not a directory\n", argv[argc - 1]);
		return 1;
	}
	for (i = 1; i < argc - 1; i++) {
		if (isdir) snprintf(path, sizeof(path), "%s/%s", argv[argc - 1], base(argv[i]));
		else snprintf(path, sizeof(path), "%s", argv[argc - 1]);
		rc |= move(argv[i], path);
	}
	return rc;
}
