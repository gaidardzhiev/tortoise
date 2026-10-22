/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *wday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int leap(int y) {
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

struct tm *gmtime_r(const time_t *t, struct tm *tm) {
	unsigned s = (unsigned)*t, days = s / 86400, rem = s % 86400;
	int y = 1970, m = 0;
	tm->tm_sec = (int)(rem % 60);
	tm->tm_min = (int)(rem / 60 % 60);
	tm->tm_hour = (int)(rem / 3600);
	tm->tm_wday = (int)((days + 4) % 7);
	while (days >= (unsigned)(leap(y) ? 366 : 365)) {
		days -= leap(y) ? 366 : 365;
		y++;
	}
	tm->tm_year = y - 1900;
	tm->tm_yday = (int)days;
	for (;;) {
		int d = mdays[m] + (m == 1 && leap(y));
		if (days < (unsigned)d) break;
		days -= (unsigned)d;
		m++;
	}
	tm->tm_mon = m;
	tm->tm_mday = (int)days + 1;
	tm->tm_isdst = 0;
	return tm;
}

struct tm *gmtime(const time_t *t) {
	static struct tm tm;
	return gmtime_r(t, &tm);
}

struct tm *localtime(const time_t *t) {
	return gmtime(t);
}

struct tm *localtime_r(const time_t *t, struct tm *tm) {
	return gmtime_r(t, tm);
}

time_t mktime(struct tm *tm) {
	int y, m;
	unsigned days = 0;
	time_t t;
	while (tm->tm_sec >= 60) {
		tm->tm_sec -= 60;
		tm->tm_min++;
	}
	while (tm->tm_min >= 60) {
		tm->tm_min -= 60;
		tm->tm_hour++;
	}
	while (tm->tm_hour >= 24) {
		tm->tm_hour -= 24;
		tm->tm_mday++;
	}
	while (tm->tm_mon >= 12) {
		tm->tm_mon -= 12;
		tm->tm_year++;
	}
	for (y = 1970; y < tm->tm_year + 1900; y++) days += leap(y) ? 366 : 365;
	for (m = 0; m < tm->tm_mon; m++) days += (unsigned)(mdays[m] + (m == 1 && leap(tm->tm_year + 1900)));
	days += (unsigned)(tm->tm_mday - 1);
	t = (time_t)(days * 86400u + (unsigned)(tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec));
	gmtime_r(&t, tm);
	return t;
}

size_t strftime(char *buf, size_t max, const char *f, const struct tm *tm) {
	size_t n = 0;
	char tmp[32];
	for (; *f; f++) {
		const char *s = tmp;
		size_t l;
		if (*f != '%') {
			tmp[0] = *f;
			tmp[1] = 0;
		} else {
			f++;
			switch (*f) {
			case 'Y': snprintf(tmp, sizeof(tmp), "%d", tm->tm_year + 1900); break;
			case 'm': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mon + 1); break;
			case 'd': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mday); break;
			case 'H': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_hour); break;
			case 'M': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_min); break;
			case 'S': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_sec); break;
			case 'j': snprintf(tmp, sizeof(tmp), "%03d", tm->tm_yday + 1); break;
			case 'a': s = wday[tm->tm_wday % 7]; break;
			case 'b': s = mon[tm->tm_mon % 12]; break;
			case '%': strcpy(tmp, "%"); break;
			case 0: f--; tmp[0] = 0; break;
			default: tmp[0] = '%'; tmp[1] = *f; tmp[2] = 0; break;
			}
		}
		l = strlen(s);
		if (n + l >= max) return 0;
		memcpy(buf + n, s, l);
		n += l;
	}
	buf[n] = 0;
	return n;
}

char *asctime(const struct tm *tm) {
	static char buf[32];
	snprintf(buf, sizeof(buf), "%s %s %2d %02d:%02d:%02d %d\n", wday[tm->tm_wday % 7], mon[tm->tm_mon % 12], tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
	return buf;
}

char *ctime(const time_t *t) {
	return asctime(gmtime(t));
}
