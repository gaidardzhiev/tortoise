/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	char name[32];
	if (argc > 1) {
		if (sethostname(argv[1], strlen(argv[1])) < 0) {
			fprintf(stderr, "hostname: cannot set the host name\n");
			return 1;
		}
		return 0;
	}
	gethostname(name, sizeof(name));
	printf("%s\n", name);
	return 0;
}
