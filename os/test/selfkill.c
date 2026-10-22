/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>

int main(void) {
	kill(getpid(), SIGTERM);
	printf("SIGTERM ignored\n");
	return 3;
}
