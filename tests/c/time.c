/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>
#include <time.h>

int main(void) {
	time_t t = 0;
	struct tm *tm, x;
	char b[64];
	tm = gmtime(&t);
	CHECK(tm->tm_year == 70 && tm->tm_mon == 0 && tm->tm_mday == 1 && tm->tm_wday == 4 && tm->tm_hour == 0);
	t = 951782400;
	tm = gmtime(&t);
	CHECK(tm->tm_year == 100 && tm->tm_mon == 1 && tm->tm_mday == 29 && tm->tm_yday == 59 && tm->tm_wday == 2);
	t = 1700000000;
	tm = localtime(&t);
	CHECK(tm->tm_year == 123 && tm->tm_mon == 10 && tm->tm_mday == 14 && tm->tm_hour == 22 && tm->tm_min == 13 && tm->tm_sec == 20);
	strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S %a %b %j %%", tm);
	CHECK(!strcmp(b, "2023-11-14 22:13:20 Tue Nov 318 %"));
	CHECK(strftime(b, 5, "%Y-%m-%d", tm) == 0);
	CHECK(!strcmp(ctime(&t), "Tue Nov 14 22:13:20 2023\n"));
	memset(&x, 0, sizeof(x));
	x.tm_year = 123;
	x.tm_mon = 10;
	x.tm_mday = 14;
	x.tm_hour = 22;
	x.tm_min = 13;
	x.tm_sec = 20;
	CHECK(mktime(&x) == 1700000000 && x.tm_wday == 2 && x.tm_yday == 317);
	x.tm_sec = 61;
	x.tm_min = 59;
	x.tm_hour = 23;
	x.tm_mday = 31;
	x.tm_mon = 11;
	x.tm_year = 99;
	CHECK(mktime(&x) == 946684801 && x.tm_year == 100 && x.tm_mon == 0 && x.tm_mday == 1);
	DONE();
}
