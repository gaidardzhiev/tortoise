/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>

int main(void) {
	int *p = (int *)16;
	printf("about to crash\n");
	return *p;
}
