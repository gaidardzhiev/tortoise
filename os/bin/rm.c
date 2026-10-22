/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static int recursive, force;

static int remove_tree(const char *path) {
	struct stat st;
	if (lstat(path, &st) < 0) {
		if (!force) fprintf(stderr, "rm: cannot remove %s\n", path);
		return !force;
	}
	if (S_ISDIR(st.st_mode) && recursive) {
		DIR *d = opendir(path);
		struct dirent *e;
		char sub[512];
		int rc = 0;
		if (d) {
			while ((e = readdir(d)) != NULL) {
				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
				snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
				rc |= remove_tree(sub);
				rewinddir(d);
			}
			closedir(d);
		}
		if (rmdir(path) < 0) {
			fprintf(stderr, "rm: cannot remove %s\n", path);
			return 1;
		}
		return rc;
	}
	if (unlink(path) < 0) {
		if (!force) fprintf(stderr, "rm: cannot remove %s\n", path);
		return !force;
	}
	return 0;
}

int main(int argc, char **argv) {
	int i = 1, rc = 0;
	while (i < argc && argv[i][0] == '-' && argv[i][1]) {
		char *o;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'r' || *o == 'R') recursive = 1;
			else if (*o == 'f') force = 1;
		}
		i++;
	}
	for (; i < argc; i++) rc |= remove_tree(argv[i]);
	return rc;
}
