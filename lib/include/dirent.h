/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _DIRENT_H
#define _DIRENT_H
struct dirent {
	unsigned d_ino;
	char d_name[28];
};
typedef struct DIR {
	int fd;
	struct dirent ent;
} DIR;
int getdirent(int fd, struct dirent *d);
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
void rewinddir(DIR *d);
int dirfd(DIR *d);
#endif
