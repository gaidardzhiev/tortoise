/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _ARPA_INET_H
#define _ARPA_INET_H
#include <netinet/in.h>
in_addr_t inet_addr(const char *s);
int inet_aton(const char *s, struct in_addr *a);
char *inet_ntoa(struct in_addr a);
#endif
