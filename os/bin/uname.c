/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

int main(int argc, char **argv) {
	struct utsname u;
	int i, s = 0, n = 0, r = 0, v = 0, m = 0, first = 1;
	uname(&u);
	for (i = 1; i < argc; i++) {
		char *o;
		if (argv[i][0] != '-') continue;
		for (o = argv[i] + 1; *o; o++) {
			if (*o == 'a') s = n = r = v = m = 1;
			else if (*o == 's') s = 1;
			else if (*o == 'n') n = 1;
			else if (*o == 'r') r = 1;
			else if (*o == 'v') v = 1;
			else if (*o == 'm') m = 1;
		}
	}
	if (!s && !n && !r && !v && !m) s = 1;
	if (s) printf("%s%s", first ? "" : " ", u.sysname), first = 0;
	if (n) printf("%s%s", first ? "" : " ", u.nodename), first = 0;
	if (r) printf("%s%s", first ? "" : " ", u.release), first = 0;
	if (v) printf("%s%s", first ? "" : " ", u.version), first = 0;
	if (m) printf("%s%s", first ? "" : " ", u.machine), first = 0;
	printf("\n");
	return 0;
}
