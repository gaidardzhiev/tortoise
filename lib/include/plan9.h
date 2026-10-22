/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _PLAN9_H
#define _PLAN9_H
#define RFNAMEG 0x01
#define RFENVG 0x02
#define RFFDG 0x04
#define RFNOTEG 0x08
#define RFPROC 0x10
#define RFNOWAIT 0x40
#define RFCNAMEG 0x400
#define RFCENVG 0x800
#define RFCFDG 0x1000
#define MREPLACE 0
#define MBEFORE 1
#define MAFTER 2
#define MCREATE 4
#define ERRMAX 128
#define NCONT 0
#define NDFLT 1
int rfork(int flags);
int bind9(const char *new, const char *old, int flags);
int mount9(int fd, int afd, const char *old, int flags, const char *aname);
int unmount(const char *new, const char *old);
int fd2path(int fd, char *buf, int n);
int errstr(char *buf, unsigned n);
int notify(void (*f)(void *ureg, char *msg));
int noted(int how);
int addns(const char *user, const char *file);
#endif
