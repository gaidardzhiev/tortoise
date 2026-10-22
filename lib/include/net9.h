/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _NET9_H
#define _NET9_H
#define NETPATHLEN 40
int dial(const char *addr, const char *local, char *dir, int *cfdp);
int announce(const char *addr, char *dir);
int listen9(const char *dir, char *newdir);
int accept9(int ctl, const char *dir);
int reject(int ctl, const char *dir, const char *cause);
int hangup(int ctl);
char *netmkaddr(const char *linear, const char *defnet, const char *defsrv);
#endif
