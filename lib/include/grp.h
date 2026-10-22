/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _GRP_H
#define _GRP_H
struct group {
	char *gr_name;
	char *gr_passwd;
	int gr_gid;
	char **gr_mem;
};
struct group *getgrnam(const char *name);
struct group *getgrgid(int gid);
struct group *getgrent(void);
void setgrent(void);
void endgrent(void);
int initgroups(const char *user, int gid);
#endif
