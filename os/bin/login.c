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
#include <plan9.h>

static void strip(char *s) {
	int n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

static void get_password(char *buf, int n) {
	struct termios t, o;
	int ok = tcgetattr(0, &o) == 0;
	printf("Password: ");
	fflush(stdout);
	if (ok) {
		t = o;
		t.c_lflag &= ~ECHO;
		tcsetattr(0, TCSANOW, &t);
	}
	if (!fgets(buf, n, stdin)) buf[0] = 0;
	if (ok) tcsetattr(0, TCSANOW, &o);
	printf("\n");
	strip(buf);
}

int main(void) {
	char name[64], pass[64], *argv[2];
	struct passwd *pw;
	for (;;) {
		printf("login: ");
		fflush(stdout);
		if (!fgets(name, sizeof(name), stdin)) return 1;
		strip(name);
		if (!name[0]) continue;
		pw = getpwnam(name);
		pass[0] = 0;
		if (!pw || pw->pw_passwd[0]) get_password(pass, sizeof(pass));
		if (pw && (!pw->pw_passwd[0] || !strcmp(crypt_t(pass, name), pw->pw_passwd))) break;
		printf("Login incorrect\n");
	}
	if (access("/lib/namespace", R_OK) == 0 && (rfork(RFNAMEG) < 0 || addns(pw->pw_name, "/lib/namespace") < 0)) fprintf(stderr, "login: cannot build the namespace\n");
	if (initgroups(pw->pw_name, pw->pw_gid) < 0 || setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
		fprintf(stderr, "login: cannot set ids\n");
		return 1;
	}
	setenv("HOME", pw->pw_dir, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("LOGNAME", pw->pw_name, 1);
	setenv("SHELL", pw->pw_shell[0] ? pw->pw_shell : "/bin/sh", 1);
	setenv("PATH", "/bin", 1);
	if (chdir(pw->pw_dir) < 0) chdir("/");
	argv[0] = "sh";
	argv[1] = NULL;
	execv(pw->pw_shell[0] ? pw->pw_shell : "/bin/sh", argv);
	fprintf(stderr, "login: cannot run %s\n", pw->pw_shell);
	return 1;
}
