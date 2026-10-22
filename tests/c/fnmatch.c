/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <fnmatch.h>

int main(void) {
	CHECK(fnmatch("*.c", "main.c", 0) == 0 && fnmatch("*.c", "main.h", 0) == FNM_NOMATCH);
	CHECK(fnmatch("a?c", "abc", 0) == 0 && fnmatch("a?c", "ac", 0) != 0);
	CHECK(fnmatch("[abc]x", "bx", 0) == 0 && fnmatch("[!abc]x", "bx", 0) != 0 && fnmatch("[!abc]x", "dx", 0) == 0);
	CHECK(fnmatch("[a-f]*", "deer", 0) == 0 && fnmatch("[a-f]*", "zebra", 0) != 0);
	CHECK(fnmatch("*", "", 0) == 0 && fnmatch("**a", "bba", 0) == 0);
	CHECK(fnmatch("\\*", "*", 0) == 0 && fnmatch("\\*", "x", 0) != 0);
	CHECK(fnmatch("*", ".hidden", FNM_PERIOD) != 0 && fnmatch(".*", ".hidden", FNM_PERIOD) == 0);
	CHECK(fnmatch("*/b", "a/b", FNM_PATHNAME) == 0 && fnmatch("*", "a/b", FNM_PATHNAME) != 0);
	CHECK(fnmatch("[]]", "]", 0) == 0);
	DONE();
}
