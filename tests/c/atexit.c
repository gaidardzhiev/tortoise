/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>

static void second(void) {
	printf("K\n");
}

static void first(void) {
	printf("O");
}

int main(void) {
	atexit(second);
	atexit(first);
	return 0;
}
