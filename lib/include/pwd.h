/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _PWD_H
#define _PWD_H
struct passwd {
	char *pw_name;
	char *pw_passwd;
	int pw_uid;
	int pw_gid;
	char *pw_gecos;
	char *pw_dir;
	char *pw_shell;
};
struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(int uid);
struct passwd *getpwent(void);
void setpwent(void);
void endpwent(void);
char *crypt_t(const char *password, const char *name);
#endif
