/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H
#include <stddef.h>
typedef int pid_t;
typedef int uid_t;
typedef int gid_t;
typedef unsigned mode_t;
typedef int off_t;
typedef unsigned dev_t;
typedef unsigned ino_t;
typedef unsigned nlink_t;
#ifndef _TIME_T
#define _TIME_T
typedef int time_t;
#endif
#endif
