/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _FNMATCH_H
#define _FNMATCH_H
#define FNM_NOMATCH 1
#define FNM_NOESCAPE 1
#define FNM_PATHNAME 2
#define FNM_PERIOD 4
int fnmatch(const char *pattern, const char *s, int flags);
#endif
