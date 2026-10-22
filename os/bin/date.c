/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <time.h>

int main(int argc, char **argv) {
	time_t t = time();
	char buf[256];
	struct tm *tm = gmtime(&t);
	const char *fmt = "%a %b %d %H:%M:%S UTC %Y";
	if (argc > 1 && argv[1][0] == '+') fmt = argv[1] + 1;
	strftime(buf, sizeof(buf), fmt, tm);
	printf("%s\n", buf);
	return 0;
}
