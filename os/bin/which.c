/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
	int i, rc = 0;
	for (i = 1; i < argc; i++) {
		char *path = getenv("PATH"), buf[256];
		int found = 0;
		if (!path) path = "/bin";
		while (*path && !found) {
			int k = 0;
			struct stat st;
			while (*path && *path != ':' && k < 200) buf[k++] = *path++;
			if (*path == ':') path++;
			if (k == 0) buf[k++] = '.';
			buf[k++] = '/';
			strncpy(buf + k, argv[i], sizeof(buf) - (unsigned)k - 1);
			buf[sizeof(buf) - 1] = 0;
			if (stat(buf, &st) == 0 && S_ISREG(st.st_mode) && access(buf, X_OK) == 0) {
				printf("%s\n", buf);
				found = 1;
			}
		}
		if (!found) rc = 1;
	}
	return rc;
}
