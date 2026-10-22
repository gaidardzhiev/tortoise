/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _NET_ROUTE_H
#define _NET_ROUTE_H
#define SIOCADDRT 0x890B
#define SIOCDELRT 0x890C
struct rtentry {
	unsigned rt_dst;
	unsigned rt_mask;
	unsigned rt_gateway;
	char rt_dev[16];
};
#endif
