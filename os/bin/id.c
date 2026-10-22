/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

static void user(const char *what, int id) {
	struct passwd *pw = getpwuid(id);
	printf("%s=%d", what, id);
	if (pw) printf("(%s)", pw->pw_name);
}

static void group(const char *what, int id) {
	struct group *g = getgrgid(id);
	if (what) printf("%s=", what);
	printf("%d", id);
	if (g) printf("(%s)", g->gr_name);
}

int main(void) {
	int list[16], n = getgroups(16, list), i;
	user("uid", getuid());
	printf(" ");
	group("gid", getgid());
	if (geteuid() != getuid()) {
		printf(" ");
		user("euid", geteuid());
	}
	if (getegid() != getgid()) {
		printf(" ");
		group("egid", getegid());
	}
	if (n > 0) {
		printf(" groups=");
		for (i = 0; i < n; i++) {
			if (i) printf(",");
			group(NULL, list[i]);
		}
	}
	printf("\n");
	return 0;
}
