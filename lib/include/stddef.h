/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _STDDEF_H
#define _STDDEF_H
typedef unsigned size_t;
typedef int ptrdiff_t;
typedef int ssize_t;
#define NULL ((void *)0)
#define offsetof(t, m) ((size_t)&((t *)0)->m)
#endif
