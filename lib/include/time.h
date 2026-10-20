/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _TIME_H
#define _TIME_H
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
struct timespec {
	unsigned tv_sec;
	unsigned tv_nsec;
};
int clock_gettime(int clk, struct timespec *ts);
int time(void);
#endif
