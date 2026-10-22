/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _SYS_TIME_H
#define _SYS_TIME_H
struct timeval {
	int tv_sec;
	int tv_usec;
};
int gettimeofday(struct timeval *tv, void *tz);
#endif
