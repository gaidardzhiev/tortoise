/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <termios.h>

int main(int argc, char **argv) {
	const char *name = argc > 1 ? argv[1] : "root";
	char pass[64], *args[2];
	struct passwd *pw = getpwnam(name);
	if (!pw) {
		fprintf(stderr, "su: no user %s\n", name);
		return 1;
	}
	if (getuid() != 0 && pw->pw_passwd[0]) {
		struct termios t, o;
		int ok = tcgetattr(0, &o) == 0, n;
		printf("Password: ");
		fflush(stdout);
		if (ok) {
			t = o;
			t.c_lflag &= ~ECHO;
			tcsetattr(0, TCSANOW, &t);
		}
		if (!fgets(pass, sizeof(pass), stdin)) pass[0] = 0;
		if (ok) tcsetattr(0, TCSANOW, &o);
		printf("\n");
		n = strlen(pass);
		while (n > 0 && pass[n - 1] == '\n') pass[--n] = 0;
		if (strcmp(crypt_t(pass, pw->pw_name), pw->pw_passwd)) {
			fprintf(stderr, "su: incorrect password\n");
			return 1;
		}
	}
	if (initgroups(pw->pw_name, pw->pw_gid) < 0 || setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
		fprintf(stderr, "su: cannot set ids\n");
		return 1;
	}
	setenv("HOME", pw->pw_dir, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("LOGNAME", pw->pw_name, 1);
	setenv("SHELL", pw->pw_shell[0] ? pw->pw_shell : "/bin/sh", 1);
	args[0] = "sh";
	args[1] = NULL;
	execv(pw->pw_shell[0] ? pw->pw_shell : "/bin/sh", args);
	fprintf(stderr, "su: cannot run %s\n", pw->pw_shell);
	return 1;
}
