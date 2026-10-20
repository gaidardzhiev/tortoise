/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _UTIME_H
#define _UTIME_H
struct utimbuf {
	unsigned actime;
	unsigned modtime;
};
int utime(const char *path, const struct utimbuf *t);
#endif
