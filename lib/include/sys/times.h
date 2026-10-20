/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H
struct tms {
	unsigned tms_utime;
	unsigned tms_stime;
	unsigned tms_cutime;
	unsigned tms_cstime;
};
int times(struct tms *t);
#endif
