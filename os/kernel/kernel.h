#ifndef KERNEL_H
#define KERNEL_H

#define NULL ((void *)0)
#define PAGE 4096
#define NPROC 64
#define NOFILE 32
#define NFILE 128
#define NPIPE 32
#define PIPESIZE 512
#define KSTACK 8192
#define USER_BASE 0x00400000u
#define USER_STACK_TOP 0x7FFFF000u
#define USER_STACK_INIT 0x10000u
#define USER_STACK_MAX 0x100000u
#define KMAP_BASE 0xE0000000u
#define DEV_VBASE 0xF0000000u
#define LOWMEM 0x00400000u

#define PTE_P 1
#define PTE_W 2
#define PTE_U 4
#define PTE_X 8
#define PTE_COW 0x200

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MMAP_TOP 0x60000000u

#define R_TEXT 1
#define R_DATA 2
#define R_HEAP 3
#define R_STACK 4
#define R_MMAP 5

#define UART_DATA 0xF0002000u
#define UART_STATUS 0xF0002004u
#define UART_CTRL 0xF0002008u
#define PIC_PENDING 0xF0000000u
#define PIC_MASK 0xF0000004u
#define PIC_ACK 0xF0000008u
#define TIMER_COUNT 0xF0001000u
#define TIMER_PERIOD 0xF0001004u
#define TIMER_CTRL 0xF0001008u
#define TIMER_PRESCALE 0xF000100Cu
#define DISK_SECTOR 0xF0003000u
#define DISK_COUNT 0xF0003004u
#define DISK_ADDR 0xF0003008u
#define DISK_CMD 0xF000300Cu
#define DISK_STATUS 0xF0003010u
#define DISK_SIZE 0xF0003014u
#define RTC_SECONDS 0xF0005000u
#define SYS_POWEROFF 0xF0007000u
#define SYS_RAMSIZE 0xF0007004u

#define ENOENT 2
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define ENOMEM 12
#define EFAULT 14
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define EMFILE 24
#define ENOSPC 28
#define ENOTEMPTY 39

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_TRUNC 0x200
#define O_APPEND 0x400

#define T_FREE 0
#define T_FILE 1
#define T_DIR 2

#define P_UNUSED 0
#define P_EMBRYO 1
#define P_RUNNABLE 2
#define P_RUNNING 3
#define P_SLEEPING 4
#define P_ZOMBIE 5
#define P_STOPPED 6

#define FD_NONE 0
#define FD_INODE 1
#define FD_PIPE 2
#define FD_CONSOLE 3
#define FD_SOCKET 4

typedef struct SigAct {
	unsigned handler, mask, flags, restorer;
} SigAct;

#define TTYBUF 512
#define ICRNL 0x100
#define OPOST 1
#define ONLCR 4
#define ISIG 1
#define ICANON 2
#define ECHO 8
#define ECHOE 0x10
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSUSP 10
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TIOCSCTTY 0x540E
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413
#define EIO 5

typedef struct Termios {
	unsigned c_iflag, c_oflag, c_cflag, c_lflag;
	unsigned char c_cc[16];
} Termios;

typedef struct TrapFrame {
	unsigned r7, r6, r5, r4, r3, r2, r1, r0;
	unsigned pc, flags, usp;
} TrapFrame;

typedef struct WaitQueue {
	int waiters;
} WaitQueue;

typedef struct Tty {
	Termios t;
	char buf[TTYBUF];
	unsigned r, w, e;
	int pgrp, sid;
	int pollers, want, hold;
	WaitQueue rwq;
	void (*out)(struct Tty *t, int c);
	void (*enable)(struct Tty *t);
	void (*disable)(struct Tty *t);
	void (*pull)(struct Tty *t);
	void *priv;
} Tty;

typedef struct Region {
	unsigned start, end, prot, flags, kind;
	struct Region *next;
} Region;

typedef struct Pipe {
	char buf[PIPESIZE];
	unsigned nread, nwrite;
	int readers, writers;
	int used;
	WaitQueue rwq, wwq;
} Pipe;


typedef struct Dirent {
	unsigned inode;
	char name[28];
} Dirent;

typedef struct Vnode Vnode;
typedef struct Fs Fs;
typedef struct Stat2 Stat2;

typedef struct FsOps {
	int (*lookup)(Vnode *dir, const char *name, Vnode **out);
	int (*create)(Vnode *dir, const char *name, unsigned mode, Vnode **out);
	int (*mkdir)(Vnode *dir, const char *name, unsigned mode);
	int (*unlink)(Vnode *dir, const char *name);
	int (*rmdir)(Vnode *dir, const char *name);
	int (*rename)(Vnode *odir, const char *oname, Vnode *ndir, const char *nname);
	int (*link)(Vnode *dir, const char *name, Vnode *target);
	int (*symlink)(Vnode *dir, const char *name, const char *target);
	int (*readlink)(Vnode *v, char *buf, unsigned n);
	int (*read)(Vnode *v, char *buf, unsigned off, unsigned n, struct File *f);
	int (*write)(Vnode *v, const char *buf, unsigned off, unsigned n, struct File *f);
	int (*readdir)(Vnode *v, unsigned *off, Dirent *de);
	int (*getattr)(Vnode *v, Stat2 *st);
	int (*setattr)(Vnode *v, int what, unsigned a, unsigned b);
	int (*truncate)(Vnode *v, unsigned len);
	int (*ioctl)(Vnode *v, unsigned req, unsigned arg, struct File *f);
	int (*poll)(Vnode *v, struct File *f);
	void (*release)(Vnode *v);
	int (*mknod)(Vnode *dir, const char *name, unsigned mode, Vnode **out);
} FsOps;

struct Stat2 {
	unsigned dev, ino, mode, nlink, uid, gid, size, atime, mtime, ctime, blocks;
};

struct Vnode {
	int ref;
	Fs *fs;
	unsigned ino;
	int type;
	unsigned rdev;
	Fs *mounted;
	void *priv;
	int nlink_zero;
	Vnode *hnext;
};

struct Fs {
	FsOps *ops;
	unsigned dev;
	void *priv;
	Vnode *root;
	Vnode *covered;
	char source[64];
	char target[128];
	char fstype[8];
	Fs *next;
};

#define VT_REG 1
#define VT_DIR 2
#define VT_LNK 3
#define VT_CHR 4
#define VT_FIFO 5
#define VT_SOCK 6

#define S_IFMT 0170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000

#define SA_MODE 1
#define SA_OWNER 2
#define SA_TIMES 3

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#define O_EXCL 0x80
#define O_NONBLOCK 0x800
#define O_DIRECTORY 0x10000
#define O_NOFOLLOW 0x20000
#define O_CLOEXEC 0x80000

#define POLLIN 1
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32

#define EPERM 1
#define EINTR 4
#define EAGAIN 11
#define EACCES 13
#define EBUSY 16
#define EXDEV 18
#define ESPIPE 29
#define ERANGE 34
#define ELOOP 40
#define ENODEV 19
#define ENAMETOOLONG 36
#define ENXIO 6
#define ENOTTY 25
#define EPIPE 32

typedef struct File {
	int type;
	int ref;
	int readable, writable, append, nonblock;
	unsigned flags;
	Vnode *vn;
	unsigned off;
	Pipe *pipe;
} File;

typedef struct Proc {
	int pid, ppid, state, status;
	unsigned pgdir;
	unsigned kstack;
	unsigned ksp;
	TrapFrame *tf;
	File *files[NOFILE];
	struct Vnode *cwd;
	unsigned char fdflags[NOFILE];
	unsigned heap_start, brk, stack_low;
	Region *regions;
	void *chan;
	unsigned wake;
	WaitQueue child_wq;
	WaitQueue sleep_wq;
	int timer_fired;
	unsigned uid, euid, suid, gid, egid, sgid;
	int pgid, sid;
	unsigned sigpend, sigmask;
	SigAct sigact[32];
	int stopsig, stop_report, cont_report, autoreap;
	int sys_nr, eintr;
	int alarm_id;
	unsigned alarm_at;
	int nice, slice;
	unsigned utime, stime, cutime, cstime;
	unsigned sys_units, sys_mark;
	int in_sys;
	struct Tty *ctty;
	char exe[128];
	char *cmdline;
	unsigned cmdlen;
	char *environ;
	unsigned envlen;
	unsigned groups[16];
	int ngroups;
	unsigned umask;
	char name[32];
} Proc;

typedef struct Stat {
	unsigned type, size, inode;
} Stat;

extern Proc procs[NPROC];
extern Proc *curproc;
extern unsigned ticks;
extern unsigned ram_size;
extern unsigned kernel_pgdir;

unsigned rd(unsigned addr);
void wr(unsigned addr, unsigned v);
unsigned get_cr(int n);
void set_cr2(unsigned v);
void set_ptbase(unsigned v);
void set_ksp(unsigned v);
void set_evt(unsigned v);
void set_ctrl(unsigned v);
void tlbflush(void);
void invlpg(unsigned va);
void cpu_idle(void);
void swtch(unsigned *old, unsigned new_sp);
void forkret(void);
void first_start(void);
void trapret(void);
extern unsigned alltraps;

void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
unsigned strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, unsigned n);
char *strncpy(char *d, const char *s, unsigned n);
void kputc(int c);
void kprintf(const char *f, ...);
void panic(const char *msg);
void poweroff(int code);

void mm_init(void);
unsigned low_alloc(void);
unsigned low_alloc_n(int n);
void low_free(unsigned pa);
void low_free_n(unsigned pa, int n);
unsigned low_free_pages(void);
unsigned low_total_pages(void);
unsigned frame_alloc(void);
void frame_free(unsigned pa);
void frame_ref(unsigned pa);
unsigned frame_count(unsigned pa);
unsigned frames_free(void);
unsigned frames_total(void);
void *kmalloc(unsigned n);
void kfree(void *p);
void kmalloc_selftest(void);
int timer_add(unsigned ticks, void (*fn)(void *), void *arg);
void timer_cancel(int id);
void timer_tick(void);
void klog_putc(int c);
int klog_read(char *dst, unsigned off, unsigned n);
Region *find_region(Proc *p, unsigned va);
int add_region(Proc *p, unsigned start, unsigned end, unsigned prot, unsigned flags, unsigned kind);
void free_regions(Region *r);
Region *copy_regions(Region *r);
int fault_in(Proc *p, unsigned va, int write);
int fork_space(Proc *src, Proc *dst);
int do_mmap(unsigned addr, unsigned len, unsigned prot, unsigned flags, int fd);
int do_munmap(unsigned addr, unsigned len);
unsigned region_flags(Region *r);
unsigned *kmap(int slot, unsigned pa);
unsigned frame_alloc_contig(unsigned n);
int devfs_is_fb(Vnode *v);
unsigned new_pgdir(void);
int map_page(unsigned pgdir, unsigned va, unsigned pa, unsigned flags);
unsigned walk(unsigned pgdir, unsigned va);
int alloc_user(unsigned pgdir, unsigned lo, unsigned hi, unsigned flags);
void free_user(unsigned pgdir, unsigned lo, unsigned hi);
void free_pgdir(unsigned pgdir);
int user_ok(unsigned va, unsigned n, int write);
int copyin(void *dst, unsigned src, unsigned n);
int copyout(unsigned dst, const void *src, unsigned n);
int copyinstr(char *dst, unsigned src, unsigned max);
int copy_to_space(unsigned pgdir, unsigned va, const void *src, unsigned n);

void proc_init(void);
void user_init(void);
void scheduler(void);
void sched(void);
void yield(void);
void sleep_on(WaitQueue *wq);
void wakeup(WaitQueue *wq);
void wakeup_one(WaitQueue *wq);
int do_fork(void);
void do_exit(int status);
int do_wait(unsigned status_ptr);
int do_kill(int pid);
int do_exec(const char *path, unsigned argv);
int do_sbrk(int incr);
int do_sleep(unsigned n);
int do_procinfo(int idx, unsigned up);
int grow_stack(unsigned va);

void trap(TrapFrame *tf);

void disk_init(void);
int disk_rw(int disk, int write, unsigned sector, void *buf);
unsigned disk_sectors(int disk);

extern Vnode *rootvn;
extern Fs *mounts;
extern FsOps tfs_ops, ramfs_ops, devfs_ops, procfs_ops;
extern WaitQueue poll_wq;
void vfs_init(void);
Vnode *vget(Fs *fs, unsigned ino, int type);
Vnode *vref(Vnode *v);
void vput(Vnode *v);
int vfs_resolve(const char *path, Vnode *cwd, int follow, Vnode **out);
int vfs_parent(const char *path, Vnode *cwd, Vnode **dir, char *name);
int vfs_mount(const char *source, const char *target, const char *fstype);
int vfs_getattr(Vnode *v, Stat2 *st);
int vfs_read(Vnode *v, char *buf, unsigned off, unsigned n);
unsigned vfs_now(void);
int tfs_mount(Fs *fs, int disk);
int ramfs_mount(Fs *fs);
int devfs_mount(Fs *fs);
int procfs_mount(Fs *fs);
int devfs_disk_of(Vnode *v);

void console_init(void);
void console_intr(void);

File *file_alloc(void);
File *file_dup(File *f);
void file_close(File *f);
int fd_alloc(File *f);
int fd_alloc_from(File *f, int min);
int sys_open(unsigned path, int flags);
int sys_read(int fd, unsigned buf, unsigned n);
int do_read(File *f, char *kbuf, unsigned n);
int write_loop(File *f, unsigned ubuf, unsigned n, char *kbuf);
int sys_write(int fd, unsigned buf, unsigned n);
int sys_close(int fd);
int sys_dup(int fd);
int sys_dup2(int a, int b);
int sys_pipe(unsigned fds);
int sys_readdir(int fd, unsigned dp);
int sys_mkdir(unsigned path);
int sys_chdir(unsigned path);
int sys_unlink(unsigned path);
int sys_rmdir(unsigned path);
int sys_stat(unsigned path, unsigned sp);
int sys_stat2(unsigned path, unsigned sp, int follow);
int sys_fstat(int fd, unsigned sp);
int sys_lseek(int fd, int off, int whence);
int sys_rename(unsigned a, unsigned b);
int sys_getcwd(unsigned buf, unsigned size);
int sys_link(unsigned a, unsigned b);
int sys_symlink(unsigned a, unsigned b);
int sys_readlink(unsigned path, unsigned buf, unsigned size);
int sys_truncate(unsigned path, unsigned len);
int sys_ftruncate(int fd, unsigned len);
int sys_utime(unsigned path, unsigned tp);
int sys_fcntl(int fd, int cmd, unsigned arg);
int sys_poll(unsigned fds, unsigned n, int timeout);
int sys_mount(unsigned src, unsigned target, unsigned type);
int sys_umount(unsigned target);
int sys_ioctl(int fd, unsigned req, unsigned arg);
int sys_chmod(unsigned path, unsigned mode);
int sys_fchmod(int fd, unsigned mode);
int vfs_umount(const char *target);
int vfs_mount_fs(Fs *fs, Vnode *target, const char *source, const char *tpath, const char *fstype);
int sys_getcwd_k(char *out, unsigned size);
int vfs_access(Vnode *v, int want);
int vfs_access_real(Vnode *v, int want);
int in_group(unsigned gid);
void tfs_umount(Fs *fs);
void tfs_invalidate(int disk);
void ramfs_umount(Fs *fs);
unsigned kernel_random(void);
File *open_vnode(Vnode *v, int flags);
unsigned pipe_id(Pipe *p);
int tty_read(Tty *t, char *buf, unsigned n, File *f);
int tty_write(Tty *t, const char *buf, unsigned n, File *f);
int tty_ioctl(Tty *t, unsigned req, unsigned arg, File *f);
int tty_poll(Tty *t, File *f);
void tty_input(Tty *t, int c);
void tty_init(Tty *t);
void tty_hangup_session(int sid);
void console_poll_hold(int on);
extern Tty contty;
int pty_lookup(Vnode *dir, const char *name, Vnode **out);
int pty_read(Vnode *v, char *buf, unsigned n, File *f);
int pty_write(Vnode *v, const char *buf, unsigned n, File *f);
int pty_readdir(Vnode *v, unsigned *off, Dirent *de, unsigned first);
int pty_getattr(Vnode *v, Stat2 *st);
int pty_ioctl(Vnode *v, unsigned req, unsigned arg, File *f);
int pty_poll(Vnode *v, File *f);
int sock_read(File *f, char *buf, unsigned n);
int sock_write(File *f, const char *buf, unsigned n);
int sock_poll(File *f);
int sock_ioctl(File *f, unsigned req, unsigned arg);
void sock_close(File *f);
void send_sigpipe(void);
int signal_pending(void);
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIG_DFL 0
#define SIG_IGN 1
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000
#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8
#define ESRCH 3
#define E2BIG 7
void send_signal(Proc *p, int sig);
int signal_group(int pgid, int sig, int check);
void deliver_signals(TrapFrame *tf);
int sys_kill2(int pid, int sig);
int sys_sigaction(int sig, unsigned act, unsigned old);
int sys_sigprocmask(int how, unsigned set, unsigned old);
int sys_sigreturn(TrapFrame *tf);
int sys_sigpending(unsigned set);
int sys_pause(void);
int sys_alarm(unsigned ticks);
int do_waitpid(int pid, unsigned stp, int options);
int sys_setpgid(int pid, int pgid);
int sys_getpgid(int pid);
int sys_setsid(void);
int sys_getsid(int pid);
int do_execve(const char *path, unsigned argv, unsigned envp);
int sys_uname(unsigned u);
int sys_clock_gettime(int clk, unsigned ts);
int sys_nice(int inc);
int sys_sethostname(unsigned name, unsigned len);
int sys_times(unsigned t);
void exit_signal(int sig);
Proc *find_pid(int pid);
int fault_signal(unsigned vec);
void reap_autoreap(void);
extern char hostname[32];


#endif
