/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

static int recursive;

static int copy_file(const char *from, const char *to, unsigned mode) {
	char buf[1024];
	int in = open(from, O_RDONLY), out, n;
	if (in < 0) {
		fprintf(stderr, "cp: cannot open %s\n", from);
		return 1;
	}
	out = open(to, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
	if (out < 0) {
		fprintf(stderr, "cp: cannot create %s\n", to);
		close(in);
		return 1;
	}
	while ((n = read(in, buf, sizeof(buf))) > 0)
		if (write(out, buf, n) != n) {
			fprintf(stderr, "cp: write error on %s\n", to);
			n = -1;
			break;
		}
	close(in);
	close(out);
	return n < 0;
}

static int copy_any(const char *from, const char *to) {
	struct stat st;
	if (stat(from, &st) < 0) {
		fprintf(stderr, "cp: cannot stat %s\n", from);
		return 1;
	}
	if (S_ISDIR(st.st_mode)) {
		DIR *d;
		struct dirent *e;
		int rc = 0;
		char a[512], b[512];
		if (!recursive) {
			fprintf(stderr, "cp: %s is a directory\n", from);
			return 1;
		}
		mkdir(to, st.st_mode & 0777);
		d = opendir(from);
		if (!d) return 1;
		while ((e = readdir(d)) != NULL) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			snprintf(a, sizeof(a), "%s/%s", from, e->d_name);
			snprintf(b, sizeof(b), "%s/%s", to, e->d_name);
			rc |= copy_any(a, b);
		}
		closedir(d);
		return rc;
	}
	return copy_file(from, to, st.st_mode);
}

static const char *base(const char *p) {
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

int main(int argc, char **argv) {
	int i = 1, rc = 0, isdir;
	struct stat st;
	char path[512];
	if (i < argc && (!strcmp(argv[i], "-r") || !strcmp(argv[i], "-R"))) {
		recursive = 1;
		i++;
	}
	if (argc - i < 2) {
		fprintf(stderr, "usage: cp [-r] from ... to\n");
		return 1;
	}
	isdir = stat(argv[argc - 1], &st) == 0 && S_ISDIR(st.st_mode);
	if (argc - i > 2 && !isdir) {
		fprintf(stderr, "cp: %s is not a directory\n", argv[argc - 1]);
		return 1;
	}
	for (; i < argc - 1; i++) {
		if (isdir) snprintf(path, sizeof(path), "%s/%s", argv[argc - 1], base(argv[i]));
		else snprintf(path, sizeof(path), "%s", argv[argc - 1]);
		rc |= copy_any(argv[i], path);
	}
	return rc;
}
