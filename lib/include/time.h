/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _TIME_H
#define _TIME_H
#include <stddef.h>
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCKS_PER_SEC 100
#ifndef _TIME_T
#define _TIME_T
typedef int time_t;
#endif
struct timespec {
	unsigned tv_sec;
	unsigned tv_nsec;
};
struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};
int clock_gettime(int clk, struct timespec *ts);
int time(void);
struct tm *gmtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *tm);
struct tm *localtime(const time_t *t);
struct tm *localtime_r(const time_t *t, struct tm *tm);
time_t mktime(struct tm *tm);
size_t strftime(char *buf, size_t max, const char *f, const struct tm *tm);
char *asctime(const struct tm *tm);
char *ctime(const time_t *t);
#endif
