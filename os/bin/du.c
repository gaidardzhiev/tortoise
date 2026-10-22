/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static int summary;

static unsigned du(const char *path, int depth) {
	struct stat st;
	unsigned total;
	if (lstat(path, &st) < 0) {
		fprintf(stderr, "du: cannot stat %s\n", path);
		return 0;
	}
	total = (st.st_blocks + 1) / 2;
	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(path);
		struct dirent *e;
		char sub[512];
		if (d) {
			while ((e = readdir(d)) != NULL) {
				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
				snprintf(sub, sizeof(sub), "%s/%s", strcmp(path, "/") ? path : "", e->d_name);
				total += du(sub, depth + 1);
			}
			closedir(d);
		}
		if (!summary || depth == 0) printf("%u\t%s\n", total, path);
	} else if (depth == 0) {
		printf("%u\t%s\n", total, path);
	}
	return total;
}

int main(int argc, char **argv) {
	int i = 1;
	if (i < argc && !strcmp(argv[i], "-s")) {
		summary = 1;
		i++;
	}
	if (i >= argc) du(".", 0);
	for (; i < argc; i++) du(argv[i], 0);
	return 0;
}
