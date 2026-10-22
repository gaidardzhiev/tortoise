/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <plan9.h>

int main(int argc, char **argv) {
	const char *file = "/lib/namespace", *user = NULL;
	struct passwd *pw;
	char *rc[3];
	int i = 1;
	while (i + 1 < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-n")) file = argv[i + 1];
		else if (!strcmp(argv[i], "-u")) user = argv[i + 1];
		else break;
		i += 2;
	}
	if (!user) {
		pw = getpwuid(getuid());
		user = pw ? pw->pw_name : "none";
	}
	if (rfork(RFNAMEG) < 0 || addns(user, file) < 0) {
		fprintf(stderr, "newns: cannot build the namespace from %s\n", file);
		return 1;
	}
	if (i < argc) {
		execv(argv[i], argv + i);
		fprintf(stderr, "newns: cannot run %s\n", argv[i]);
		return 1;
	}
	rc[0] = "rc";
	rc[1] = NULL;
	execv("/bin/rc", rc);
	fprintf(stderr, "newns: cannot run /bin/rc\n");
	return 1;
}
