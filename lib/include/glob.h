/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _GLOB_H
#define _GLOB_H
#include <stddef.h>
#define GLOB_NOCHECK 16
#define GLOB_NOSPACE 1
#define GLOB_NOMATCH 3
typedef struct {
	size_t gl_pathc;
	char **gl_pathv;
	size_t gl_offs;
} glob_t;
int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *g);
void globfree(glob_t *g);
#endif
