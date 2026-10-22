/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _UNISTD_H
#define _UNISTD_H
#include <stddef.h>
struct procinfo {
	int pid;
	int ppid;
	int state;
	char name[32];
};
int read(int fd, void *buf, int n);
int write(int fd, const void *buf, int n);
int close(int fd);
void _exit(int status);
void *sbrk(int incr);
int fork(void);
int wait(int *status);
int exec(const char *path, char **argv);
int execv(const char *path, char **argv);
int getpid(void);
int unlink(const char *path);
int mkdir(const char *path, unsigned mode);
int chdir(const char *path);
int dup(int fd);
int pipe(int *fds);
int kill(int pid, int sig);
int getppid(void);
int execve(const char *path, char **argv, char **envp);
int execvp(const char *file, char **argv);
int setpgid(int pid, int pgid);
int getpgid(int pid);
int getpgrp(void);
int setsid(void);
int getsid(int pid);
int pause(void);
unsigned alarm(unsigned ticks);
int nice(int inc);
int sethostname(const char *name, unsigned len);
int gethostname(char *name, unsigned len);
extern char **environ;
int isatty(int fd);
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);
int setuid(int uid);
int setgid(int gid);
int setreuid(int r, int e);
int setregid(int r, int e);
int getgroups(int n, int *list);
int setgroups(int n, const int *list);
int chown(const char *path, int uid, int gid);
int fchown(int fd, int uid, int gid);
int access(const char *path, int mode);
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
char *ttyname(int fd);
unsigned sleep(unsigned seconds);
int usleep(unsigned usec);
int execl(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execle(const char *path, const char *arg, ...);
int brk(void *addr);
int getopt(int argc, char *const argv[], const char *opts);
extern char *optarg;
extern int optind, opterr, optopt;
int time(void);
int poweroff(int code);
int procinfo(int index, struct procinfo *p);
int lseek(int fd, int off, int whence);
int dup2(int a, int b);
int rename(const char *a, const char *b);
int rmdir(const char *path);
char *getcwd(char *buf, unsigned size);
int link(const char *a, const char *b);
int symlink(const char *target, const char *path);
int readlink(const char *path, char *buf, unsigned size);
int truncate(const char *path, unsigned len);
int ftruncate(int fd, unsigned len);
int mount(const char *source, const char *target, const char *type);
int umount(const char *target);
int sync(void);
int ioctl(int fd, unsigned req, ...);
int stat_old(const char *path, unsigned *st);
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
