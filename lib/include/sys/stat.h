/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#define S_IFMT 0170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
struct stat {
	unsigned st_dev;
	unsigned st_ino;
	unsigned st_mode;
	unsigned st_nlink;
	unsigned st_uid;
	unsigned st_gid;
	unsigned st_size;
	unsigned st_atime;
	unsigned st_mtime;
	unsigned st_ctime;
	unsigned st_blocks;
};
int stat(const char *path, struct stat *st);
int lstat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int chmod(const char *path, unsigned mode);
int fchmod(int fd, unsigned mode);
int mkdir(const char *path, unsigned mode);
unsigned umask(unsigned mask);
#endif
