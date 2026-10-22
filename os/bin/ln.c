/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
	int i = 1, sym = 0, r;
	char path[512];
	struct stat st;
	const char *to, *s;
	if (i < argc && !strcmp(argv[i], "-s")) {
		sym = 1;
		i++;
	}
	if (argc - i != 2) {
		fprintf(stderr, "usage: ln [-s] target name\n");
		return 1;
	}
	to = argv[i + 1];
	if (stat(to, &st) == 0 && S_ISDIR(st.st_mode)) {
		s = strrchr(argv[i], '/');
		snprintf(path, sizeof(path), "%s/%s", to, s ? s + 1 : argv[i]);
		to = path;
	}
	r = sym ? symlink(argv[i], to) : link(argv[i], to);
	if (r < 0) {
		fprintf(stderr, "ln: cannot link %s to %s\n", to, argv[i]);
		return 1;
	}
	return 0;
}
