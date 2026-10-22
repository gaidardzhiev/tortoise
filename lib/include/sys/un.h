/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _SYS_UN_H
#define _SYS_UN_H
#include <sys/socket.h>
struct sockaddr_un {
	sa_family_t sun_family;
	char sun_path[108];
};
#endif
