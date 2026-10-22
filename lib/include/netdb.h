/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _NETDB_H
#define _NETDB_H
#include <netinet/in.h>
struct hostent {
	char *h_name;
	char **h_aliases;
	int h_addrtype;
	int h_length;
	char **h_addr_list;
};
#define h_addr h_addr_list[0]
struct servent {
	char *s_name;
	char **s_aliases;
	int s_port;
	char *s_proto;
};
struct hostent *gethostbyname(const char *name);
struct servent *getservbyname(const char *name, const char *proto);
#endif
