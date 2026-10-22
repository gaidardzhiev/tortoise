/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>

int main(void) {
	char buf[512];
	if (!getcwd(buf, sizeof(buf))) {
		fprintf(stderr, "pwd: cannot get the directory\n");
		return 1;
	}
	printf("%s\n", buf);
	return 0;
}
