/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char **argv) {
	int i;
	char c;
	for (i = 0; i < argc; i++) printf("argv[%d] = %s\n", i, argv[i]);
	for (i = 0; environ && environ[i]; i++) printf("env %s\n", environ[i]);
	printf("fd 5 %s\n", read(5, &c, 0) < 0 ? "closed" : "open");
	printf("fd 6 %s\n", read(6, &c, 0) < 0 ? "closed" : "open");
	return 0;
}
