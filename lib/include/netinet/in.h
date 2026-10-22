/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#include <sys/socket.h>
#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define INADDR_ANY 0u
#define INADDR_LOOPBACK 0x7F000001u
#define INADDR_BROADCAST 0xFFFFFFFFu
#define INADDR_NONE 0xFFFFFFFFu
#define TCP_NODELAY 1
typedef unsigned short in_port_t;
typedef unsigned in_addr_t;
struct in_addr {
	in_addr_t s_addr;
};
struct sockaddr_in {
	sa_family_t sin_family;
	in_port_t sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};
unsigned short htons(unsigned short v);
unsigned short ntohs(unsigned short v);
unsigned htonl(unsigned v);
unsigned ntohl(unsigned v);
#endif
