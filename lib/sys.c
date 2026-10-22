/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <poll.h>
#include <utime.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <termios.h>
#include <dirent.h>
#include <sys/time.h>
#include <stdio.h>

int __syscall(int n, int a, int b, int c, int d, int e);
int errno;

static int ret(int r) {
	if (r < 0) {
		errno = -r;
		return -1;
	}
	return r;
}

void _exit(int status) {
	__syscall(1, status, 0, 0, 0, 0);
	for (;;) ;
}

int fork(void) { return ret(__syscall(2, 0, 0, 0, 0, 0)); }
int read(int fd, void *buf, int n) { return ret(__syscall(3, fd, (int)buf, n, 0, 0)); }
int write(int fd, const void *buf, int n) { return ret(__syscall(4, fd, (int)buf, n, 0, 0)); }

int open(const char *path, int flags, ...) {
	int mode = 0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = (int)(0x80000000u | (va_arg(ap, unsigned) & 07777));
		va_end(ap);
	}
	return ret(__syscall(5, (int)path, flags, mode, 0, 0));
}

int close(int fd) { return ret(__syscall(6, fd, 0, 0, 0, 0)); }
int wait(int *status) { return ret(__syscall(7, (int)status, 0, 0, 0, 0)); }
int exec(const char *path, char **argv) { return ret(__syscall(8, (int)path, (int)argv, 0, 0, 0)); }
int execv(const char *path, char **argv) { return execve(path, argv, environ); }

void *sbrk(int incr) {
	int r = __syscall(9, incr, 0, 0, 0, 0);
	if (r < 0 && r > -4096) {
		errno = -r;
		return (void *)-1;
	}
	return (void *)r;
}

int getpid(void) { return __syscall(10, 0, 0, 0, 0, 0); }
int unlink(const char *path) { return ret(__syscall(11, (int)path, 0, 0, 0, 0)); }
int mkdir(const char *path, unsigned mode) { return ret(__syscall(12, (int)path, (int)(0x80000000u | (mode & 07777)), 0, 0, 0)); }
int chdir(const char *path) { return ret(__syscall(13, (int)path, 0, 0, 0, 0)); }
int getdirent(int fd, struct dirent *d) { return ret(__syscall(14, fd, (int)d, 0, 0, 0)); }
int dup(int fd) { return ret(__syscall(15, fd, 0, 0, 0, 0)); }
int pipe(int *fds) { return ret(__syscall(16, (int)fds, 0, 0, 0, 0)); }
int kill(int pid, int sig) { return ret(__syscall(44, pid, sig, 0, 0, 0)); }
unsigned sleep(unsigned seconds) { return __syscall(18, (int)(seconds * 100), 0, 0, 0, 0) < 0 ? seconds : 0; }
int usleep(unsigned usec) { return ret(__syscall(18, (int)((usec + 9999) / 10000), 0, 0, 0, 0)); }
int time(void) { return __syscall(19, 0, 0, 0, 0, 0); }
int poweroff(int code) { return ret(__syscall(20, code, 0, 0, 0, 0)); }
int stat_old(const char *path, unsigned *st) { return ret(__syscall(21, (int)path, (int)st, 0, 0, 0)); }
int stat(const char *path, struct stat *st) { return ret(__syscall(84, (int)path, (int)st, 0, 0, 0)); }
int lstat(const char *path, struct stat *st) { return ret(__syscall(85, (int)path, (int)st, 0, 0, 0)); }
int fstat(int fd, struct stat *st) { return ret(__syscall(26, fd, (int)st, 0, 0, 0)); }
int lseek(int fd, int off, int whence) { return ret(__syscall(25, fd, off, whence, 0, 0)); }
int dup2(int a, int b) { return ret(__syscall(27, a, b, 0, 0, 0)); }
int rename(const char *a, const char *b) { return ret(__syscall(28, (int)a, (int)b, 0, 0, 0)); }
int rmdir(const char *path) { return ret(__syscall(29, (int)path, 0, 0, 0, 0)); }
char *getcwd(char *buf, unsigned size) { return ret(__syscall(30, (int)buf, (int)size, 0, 0, 0)) < 0 ? (char *)0 : buf; }
int link(const char *a, const char *b) { return ret(__syscall(31, (int)a, (int)b, 0, 0, 0)); }
int symlink(const char *a, const char *b) { return ret(__syscall(32, (int)a, (int)b, 0, 0, 0)); }
int readlink(const char *p, char *buf, unsigned n) { return ret(__syscall(33, (int)p, (int)buf, (int)n, 0, 0)); }
int truncate(const char *p, unsigned len) { return ret(__syscall(63, (int)p, (int)len, 0, 0, 0)); }
int ftruncate(int fd, unsigned len) { return ret(__syscall(64, fd, (int)len, 0, 0, 0)); }
int utime(const char *p, const struct utimbuf *t) { return ret(__syscall(65, (int)p, (int)t, 0, 0, 0)); }
int mount(const char *s, const char *t, const char *ty) { return ret(__syscall(60, (int)s, (int)t, (int)ty, 0, 0)); }
int umount(const char *t) { return ret(__syscall(61, (int)t, 0, 0, 0, 0)); }
int sync(void) { return ret(__syscall(86, 0, 0, 0, 0, 0)); }
int poll(struct pollfd *f, unsigned n, int t) { return ret(__syscall(57, (int)f, (int)n, t, 0, 0)); }
int fcntl(int fd, int cmd, ...) {
	va_list ap;
	int a;
	va_start(ap, cmd);
	a = va_arg(ap, int);
	va_end(ap);
	return ret(__syscall(55, fd, cmd, a, 0, 0));
}
int ioctl(int fd, unsigned req, ...) {
	va_list ap;
	int a;
	va_start(ap, req);
	a = va_arg(ap, int);
	va_end(ap);
	return ret(__syscall(56, fd, (int)req, a, 0, 0));
}
int procinfo(int index, struct procinfo *p) { return ret(__syscall(22, index, (int)p, 0, 0, 0)); }

void *mmap(void *addr, unsigned len, int prot, int flags, int fd, int off) {
	int r = __syscall(58, (int)addr, (int)len, prot, flags, fd);
	(void)off;
	if (r < 0 && r > -4096) {
		errno = -r;
		return (void *)-1;
	}
	return (void *)r;
}

int munmap(void *addr, unsigned len) { return ret(__syscall(59, (int)addr, (int)len, 0, 0, 0)); }

int chmod(const char *path, unsigned mode) { return ret(__syscall(34, (int)path, (int)mode, 0, 0, 0)); }
int fchmod(int fd, unsigned mode) { return ret(__syscall(92, fd, (int)mode, 0, 0, 0)); }

int waitpid(int pid, int *status, int options) { return ret(__syscall(23, pid, (int)status, options, 0, 0)); }
int getppid(void) { return __syscall(24, 0, 0, 0, 0, 0); }
int execve(const char *path, char **argv, char **envp) { return ret(__syscall(62, (int)path, (int)argv, (int)envp, 0, 0)); }
int setpgid(int pid, int pgid) { return ret(__syscall(51, pid, pgid, 0, 0, 0)); }
int getpgid(int pid) { return ret(__syscall(52, pid, 0, 0, 0, 0)); }
int getpgrp(void) { return getpgid(0); }
int setsid(void) { return ret(__syscall(53, 0, 0, 0, 0, 0)); }
int getsid(int pid) { return ret(__syscall(54, pid, 0, 0, 0, 0)); }
int pause(void) { return ret(__syscall(49, 0, 0, 0, 0, 0)); }
unsigned alarm(unsigned ticks) { return (unsigned)__syscall(50, (int)ticks, 0, 0, 0, 0); }
int uname(struct utsname *u) { return ret(__syscall(66, (int)u, 0, 0, 0, 0)); }
int clock_gettime(int clk, struct timespec *ts) { return ret(__syscall(67, clk, (int)ts, 0, 0, 0)); }
int sethostname(const char *name, unsigned len) { return ret(__syscall(69, (int)name, (int)len, 0, 0, 0)); }
int times(struct tms *t) { return ret(__syscall(87, (int)t, 0, 0, 0, 0)); }

int nice(int inc) {
	int r = __syscall(68, inc, 0, 0, 0, 0);
	if (r == -EPERM) {
		errno = EPERM;
		return -1;
	}
	return r;
}

int gethostname(char *name, unsigned len) {
	struct utsname u;
	if (uname(&u) < 0) return -1;
	strncpy(name, u.nodename, len);
	if (len) name[len - 1] = 0;
	return 0;
}

int execvp(const char *file, char **argv) {
	char buf[256], *path;
	int i, k;
	if (strchr(file, '/')) return execv(file, argv);
	path = getenv("PATH");
	if (!path) path = "/bin";
	while (*path) {
		for (i = 0; path[i] && path[i] != ':' && i < 200; i++) buf[i] = path[i];
		k = i;
		if (k == 0) buf[k++] = '.';
		buf[k++] = '/';
		strncpy(buf + k, file, sizeof(buf) - k - 1);
		buf[sizeof(buf) - 1] = 0;
		execv(buf, argv);
		path += i;
		if (*path == ':') path++;
	}
	return -1;
}

void __sigtramp(void);

int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
	struct sigaction a;
	if (act) {
		a = *act;
		a.sa_restorer = __sigtramp;
		return ret(__syscall(45, sig, (int)&a, (int)old, 0, 0));
	}
	return ret(__syscall(45, sig, 0, (int)old, 0, 0));
}

sighandler_t signal(int sig, sighandler_t h) {
	struct sigaction a, o;
	a.sa_handler = h;
	a.sa_mask = 0;
	a.sa_flags = SA_RESTART;
	if (sigaction(sig, &a, &o) < 0) return SIG_ERR;
	return o.sa_handler;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old) { return ret(__syscall(46, how, (int)set, (int)old, 0, 0)); }
int sigpending(sigset_t *set) { return ret(__syscall(48, (int)set, 0, 0, 0, 0)); }
int raise(int sig) { return kill(getpid(), sig); }
int sigemptyset(sigset_t *s) { *s = 0; return 0; }
int sigfillset(sigset_t *s) { *s = 0xFFFFFFFE; return 0; }

int sigaddset(sigset_t *s, int sig) {
	if (sig <= 0 || sig >= 32) {
		errno = EINVAL;
		return -1;
	}
	*s |= 1u << sig;
	return 0;
}

int sigdelset(sigset_t *s, int sig) {
	if (sig <= 0 || sig >= 32) {
		errno = EINVAL;
		return -1;
	}
	*s &= ~(1u << sig);
	return 0;
}

int sigismember(const sigset_t *s, int sig) {
	if (sig <= 0 || sig >= 32) return 0;
	return (*s >> sig) & 1;
}

int tcgetattr(int fd, struct termios *t) { return ioctl(fd, TCGETS, t); }

int tcsetattr(int fd, int how, const struct termios *t) {
	if (how < 0 || how > 2) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(fd, how == TCSANOW ? TCSETS : TCSETSW, t);
}

void cfmakeraw(struct termios *t) {
	t->c_iflag &= ~ICRNL;
	t->c_oflag &= ~OPOST;
	t->c_lflag &= ~(ISIG | ICANON | ECHO | ECHOE);
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}

int tcgetpgrp(int fd) {
	int pg;
	if (ioctl(fd, TIOCGPGRP, &pg) < 0) return -1;
	return pg;
}

int tcsetpgrp(int fd, int pgrp) { return ioctl(fd, TIOCSPGRP, &pgrp); }

int isatty(int fd) {
	struct termios t;
	return ioctl(fd, TCGETS, &t) == 0;
}

char *ttyname(int fd) {
	static char name[32];
	struct stat st;
	if (!isatty(fd) || fstat(fd, &st) < 0) {
		errno = ENOTTY;
		return NULL;
	}
	if (st.st_dev == 0x401) strcpy(name, "/dev/console");
	else if (st.st_dev == 0x400) strcpy(name, "/dev/tty");
	else snprintf(name, sizeof(name), "/dev/pts/%d", st.st_dev & 0xFF);
	return name;
}

int chown(const char *path, int uid, int gid) { return ret(__syscall(35, (int)path, uid, gid, 0, 0)); }
int fchown(int fd, int uid, int gid) { return ret(__syscall(93, fd, uid, gid, 0, 0)); }
unsigned umask(unsigned mask) { return (unsigned)__syscall(36, (int)mask, 0, 0, 0, 0); }
int access(const char *path, int mode) { return ret(__syscall(37, (int)path, mode, 0, 0, 0)); }
int getuid(void) { return __syscall(38, 0, 0, 0, 0, 0); }
int geteuid(void) { return __syscall(39, 0, 0, 0, 0, 0); }
int getgid(void) { return __syscall(40, 0, 0, 0, 0, 0); }
int getegid(void) { return __syscall(41, 0, 0, 0, 0, 0); }
int setuid(int uid) { return ret(__syscall(42, uid, 0, 0, 0, 0)); }
int setgid(int gid) { return ret(__syscall(43, gid, 0, 0, 0, 0)); }
int setreuid(int r, int e) { return ret(__syscall(88, r, e, 0, 0, 0)); }
int setregid(int r, int e) { return ret(__syscall(89, r, e, 0, 0, 0)); }
int getgroups(int n, int *list) { return ret(__syscall(90, n, (int)list, 0, 0, 0)); }
int setgroups(int n, const int *list) { return ret(__syscall(91, n, (int)list, 0, 0, 0)); }

static int collect(const char *arg, va_list ap, char **argv, char ***envp) {
	int n = 0;
	argv[n++] = (char *)arg;
	while (arg && n < 63) {
		arg = va_arg(ap, char *);
		argv[n++] = (char *)arg;
	}
	argv[n] = NULL;
	if (envp) *envp = va_arg(ap, char **);
	return n;
}

int execl(const char *path, const char *arg, ...) {
	char *argv[64];
	va_list ap;
	va_start(ap, arg);
	collect(arg, ap, argv, NULL);
	va_end(ap);
	return execv(path, argv);
}

int execlp(const char *file, const char *arg, ...) {
	char *argv[64];
	va_list ap;
	va_start(ap, arg);
	collect(arg, ap, argv, NULL);
	va_end(ap);
	return execvp(file, argv);
}

int execle(const char *path, const char *arg, ...) {
	char *argv[64], **envp;
	va_list ap;
	va_start(ap, arg);
	collect(arg, ap, argv, &envp);
	va_end(ap);
	return execve(path, argv, envp);
}

int brk(void *addr) {
	char *cur = sbrk(0);
	if (sbrk((char *)addr - cur) == (void *)-1) return -1;
	return 0;
}

void abort(void) {
	signal(SIGABRT, SIG_DFL);
	raise(SIGABRT);
	_exit(134);
}

int gettimeofday(struct timeval *tv, void *tz) {
	struct timespec ts;
	(void)tz;
	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) return -1;
	tv->tv_sec = (int)ts.tv_sec;
	tv->tv_usec = (int)(ts.tv_nsec / 1000);
	return 0;
}
