/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>

static int number(const char *s, int *out) {
	int n = 0;
	if (!*s) return 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9') return 0;
		n = n * 10 + *s - '0';
	}
	*out = n;
	return 1;
}

int main(int argc, char **argv) {
	char spec[64], *colon;
	int uid, gid = -1, i, rc = 0;
	if (argc < 3) {
		fprintf(stderr, "usage: chown user[:group] file ...\n");
		return 1;
	}
	strncpy(spec, argv[1], sizeof(spec) - 1);
	spec[sizeof(spec) - 1] = 0;
	colon = strchr(spec, ':');
	if (colon) *colon++ = 0;
	if (!number(spec, &uid)) {
		struct passwd *pw = getpwnam(spec);
		if (!pw) {
			fprintf(stderr, "chown: no user %s\n", spec);
			return 1;
		}
		uid = pw->pw_uid;
	}
	if (colon && *colon && !number(colon, &gid)) {
		struct group *g = getgrnam(colon);
		if (!g) {
			fprintf(stderr, "chown: no group %s\n", colon);
			return 1;
		}
		gid = g->gr_gid;
	}
	for (i = 2; i < argc; i++) {
		struct stat st;
		if (stat(argv[i], &st) < 0) {
			fprintf(stderr, "chown: %s: no such file\n", argv[i]);
			rc = 1;
			continue;
		}
		if (chown(argv[i], uid, gid < 0 ? (int)st.st_gid : gid) < 0) {
			fprintf(stderr, "chown: %s: operation not permitted\n", argv[i]);
			rc = 1;
		}
	}
	return rc;
}
