/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
	fflush(stdout);
	poweroff(argc > 1 ? atoi(argv[1]) : 0);
	return 1;
}
