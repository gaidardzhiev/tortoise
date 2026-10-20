/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H
#include <termios.h>
int ioctl(int fd, unsigned req, ...);
#endif
