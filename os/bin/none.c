/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <plan9.h>

int main(int argc, char **argv) {
	struct passwd *pw = getpwnam("none");
	char *rc[2];
	int g;
	if (!pw) {
		fprintf(stderr, "auth/none: no user none\n");
		return 1;
	}
	if (geteuid() != 0) {
		fprintf(stderr, "auth/none: only the super user may become none\n");
		return 1;
	}
	if (rfork(RFNAMEG | RFENVG) < 0 || addns("none", "/lib/namespace") < 0) {
		fprintf(stderr, "auth/none: cannot build the namespace\n");
		return 1;
	}
	g = pw->pw_gid;
	if (setgroups(1, &g) < 0 || setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
		fprintf(stderr, "auth/none: cannot become none\n");
		return 1;
	}
	chdir(pw->pw_dir);
	if (argc > 1) {
		execv(argv[1], argv + 1);
		fprintf(stderr, "auth/none: cannot run %s\n", argv[1]);
		return 1;
	}
	rc[0] = "rc";
	rc[1] = NULL;
	execv("/bin/rc", rc);
	fprintf(stderr, "auth/none: cannot run /bin/rc\n");
	return 1;
}
