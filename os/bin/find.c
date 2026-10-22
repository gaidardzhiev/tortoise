/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>

static const char *name_pat;
static int type;

static void walk(const char *path) {
	struct stat st;
	const char *base = strrchr(path, '/');
	int ok = 1;
	if (lstat(path, &st) < 0) {
		fprintf(stderr, "find: cannot stat %s\n", path);
		return;
	}
	base = base && base[1] ? base + 1 : path;
	if (name_pat && fnmatch(name_pat, base, 0) != 0) ok = 0;
	if (type == 'f' && !S_ISREG(st.st_mode)) ok = 0;
	if (type == 'd' && !S_ISDIR(st.st_mode)) ok = 0;
	if (type == 'l' && !S_ISLNK(st.st_mode)) ok = 0;
	if (ok) printf("%s\n", path);
	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(path);
		struct dirent *e;
		char sub[512];
		if (!d) return;
		while ((e = readdir(d)) != NULL) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			snprintf(sub, sizeof(sub), "%s/%s", strcmp(path, "/") ? path : "", e->d_name);
			walk(sub);
		}
		closedir(d);
	}
}

int main(int argc, char **argv) {
	int i = 1, first, last;
	first = i;
	while (i < argc && argv[i][0] != '-') i++;
	last = i;
	for (; i < argc; i++) {
		if (!strcmp(argv[i], "-name") && i + 1 < argc) name_pat = argv[++i];
		else if (!strcmp(argv[i], "-type") && i + 1 < argc) type = argv[++i][0];
		else {
			fprintf(stderr, "find: unknown expression %s\n", argv[i]);
			return 1;
		}
	}
	if (first == last) walk(".");
	for (i = first; i < last; i++) walk(argv[i]);
	return 0;
}
