/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <termios.h>
#include <sys/stat.h>

static char lines[64][256];

static void ask(const char *prompt, char *buf, int n) {
	struct termios t, o;
	int ok = tcgetattr(0, &o) == 0, l;
	printf("%s", prompt);
	fflush(stdout);
	if (ok) {
		t = o;
		t.c_lflag &= ~ECHO;
		tcsetattr(0, TCSANOW, &t);
	}
	if (!fgets(buf, n, stdin)) buf[0] = 0;
	if (ok) tcsetattr(0, TCSANOW, &o);
	printf("\n");
	l = strlen(buf);
	while (l > 0 && buf[l - 1] == '\n') buf[--l] = 0;
}

int main(int argc, char **argv) {
	char name[64], old[64], p1[64], p2[64], hash[16];
	struct passwd *pw;
	FILE *f;
	int n = 0, i, found = 0, l;
	if (argc > 1) {
		strncpy(name, argv[1], sizeof(name) - 1);
		name[sizeof(name) - 1] = 0;
	} else {
		pw = getpwuid(getuid());
		if (!pw) {
			fprintf(stderr, "passwd: who are you?\n");
			return 1;
		}
		strcpy(name, pw->pw_name);
	}
	pw = getpwnam(name);
	if (!pw) {
		fprintf(stderr, "passwd: no user %s\n", name);
		return 1;
	}
	if (getuid() != 0 && getuid() != pw->pw_uid) {
		fprintf(stderr, "passwd: permission denied\n");
		return 1;
	}
	if (getuid() != 0 && pw->pw_passwd[0]) {
		ask("Old password: ", old, sizeof(old));
		if (strcmp(crypt_t(old, name), pw->pw_passwd)) {
			fprintf(stderr, "passwd: incorrect password\n");
			return 1;
		}
	}
	ask("New password: ", p1, sizeof(p1));
	ask("Retype new password: ", p2, sizeof(p2));
	if (strcmp(p1, p2)) {
		fprintf(stderr, "passwd: passwords do not match\n");
		return 1;
	}
	strcpy(hash, p1[0] ? crypt_t(p1, name) : "");
	f = fopen("/etc/passwd", "r");
	if (!f) {
		fprintf(stderr, "passwd: cannot read /etc/passwd\n");
		return 1;
	}
	while (n < 64 && fgets(lines[n], sizeof(lines[n]), f)) n++;
	fclose(f);
	f = fopen("/etc/passwd.tmp", "w");
	if (!f) {
		fprintf(stderr, "passwd: cannot write /etc/passwd\n");
		return 1;
	}
	l = strlen(name);
	for (i = 0; i < n; i++) {
		char *p = lines[i];
		if (!strncmp(p, name, l) && p[l] == ':') {
			char *rest = strchr(p + l + 1, ':');
			fprintf(f, "%s:%s%s", name, hash, rest ? rest : ":\n");
			found = 1;
		} else {
			fputs(p, f);
		}
	}
	fclose(f);
	chmod("/etc/passwd.tmp", 0644);
	if (!found || rename("/etc/passwd.tmp", "/etc/passwd") < 0) {
		unlink("/etc/passwd.tmp");
		fprintf(stderr, "passwd: cannot update /etc/passwd\n");
		return 1;
	}
	printf("passwd: password changed for %s\n", name);
	return 0;
}
