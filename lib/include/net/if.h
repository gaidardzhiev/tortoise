/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _NET_IF_H
#define _NET_IF_H
#define IFNAMSIZ 16
#define IFF_UP 1
#define IFF_LOOPBACK 8
#define IFF_RUNNING 0x40
#define IFF_PROMISC 0x100
#define SIOCGIFCONF 0x8912
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
#define SIOCGIFADDR 0x8915
#define SIOCSIFADDR 0x8916
#define SIOCGIFNETMASK 0x891B
#define SIOCSIFNETMASK 0x891C
#define SIOCGIFHWADDR 0x8927
struct ifreq {
	char ifr_name[IFNAMSIZ];
	unsigned ifr_data[4];
};
struct ifconf {
	int ifc_len;
	struct ifreq *ifc_req;
};
#endif
