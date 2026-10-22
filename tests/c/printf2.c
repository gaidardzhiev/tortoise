/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>

int main(void) {
	char b[64];
	long l = -123456L;
	unsigned long ul = 4000000000UL;
	sprintf(b, "%ld %lu %lx", l, ul, 255UL);
	CHECK(!strcmp(b, "-123456 4000000000 ff"));
	sprintf(b, "[%*.*s]", 6, 3, "abcdef");
	CHECK(!strcmp(b, "[   abc]"));
	sprintf(b, "[%-*.*s]", 5, 2, "xyz");
	CHECK(!strcmp(b, "[xy   ]"));
	sprintf(b, "[%.*s]", 0, "abc");
	CHECK(!strcmp(b, "[]"));
	sprintf(b, "[%*d]", -4, 7);
	CHECK(!strcmp(b, "[7   ]"));
	DONE();
}
