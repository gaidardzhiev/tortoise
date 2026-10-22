/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define K_ROOT 1
#define K_MEMINFO 2
#define K_UPTIME 3
#define K_LOADAVG 4
#define K_MOUNTS 5
#define K_VERSION 6
#define K_CPUINFO 7
#define K_SELF 8
#define K_NET 9
#define K_NETF 0x20
#define K_PID 0x10
#define K_STATUS 0x11
#define K_CMDLINE 0x12
#define K_ENVIRON 0x13
#define K_CWD 0x14
#define K_EXE 0x15
#define K_MAPS 0x16
#define K_FD 0x17
#define K_NS 0x18
#define K_CTL 0x19
#define K_NOTE 0x1A
#define K_NOTEPG 0x1B
#define K_FDTAB 0x1C
#define K_ARGS 0x1D
#define K_FDLINK 0x80
#define GENMAX 8192

static unsigned procfs_dev;

static const char *top_names[] = {"meminfo", "uptime", "loadavg", "mounts", "version", "cpuinfo", "self", "net"};
static const char *net_names[] = {"dev", "tcp", "udp", "unix", "arp", "route"};
static const char *pid_names[] = {"status", "cmdline", "environ", "cwd", "exe", "maps", "fd", "ns", "ctl", "note", "notepg", "fdtab", "args"};
#define NPIDF 13

#define KIND(v) ((v)->ino & 0xFF)
#define PID(v) ((int)((v)->ino >> 8))

static int vtype_of(unsigned kind) {
	if (kind == K_ROOT || kind == K_PID || kind == K_FD || kind == K_NET) return VT_DIR;
	if (kind == K_SELF || kind == K_CWD || kind == K_EXE || kind >= K_FDLINK) return VT_LNK;
	return VT_REG;
}

static Vnode *node(Fs *fs, int pid, unsigned kind) {
	return vget(fs, ((unsigned)pid << 8) | kind, vtype_of(kind));
}

static Proc *live(int pid) {
	Proc *p = find_pid(pid);
	if (!p || p->state == P_UNUSED) return NULL;
	return p;
}

static int is_num(const char *s, int *out) {
	int n = 0;
	if (!*s) return 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9' || n > 100000) return 0;
		n = n * 10 + (*s - '0');
	}
	*out = n;
	return 1;
}

static int p_lookup(Vnode *dir, const char *name, Vnode **out) {
	unsigned kind = KIND(dir), i;
	int pid = PID(dir), n;
	if (!strcmp(name, ".")) {
		*out = vref(dir);
		return 0;
	}
	if (kind == K_ROOT) {
		if (!strcmp(name, "..")) {
			*out = vref(dir);
			return 0;
		}
		for (i = 0; i < 8; i++)
			if (!strcmp(name, top_names[i])) {
				*out = node(dir->fs, 0, K_MEMINFO + i);
				return *out ? 0 : -ENOMEM;
			}
		if (is_num(name, &n) && live(n)) {
			*out = node(dir->fs, n, K_PID);
			return *out ? 0 : -ENOMEM;
		}
		return -ENOENT;
	}
	if (kind == K_NET) {
		if (!strcmp(name, "..")) {
			*out = vref(dir->fs->root);
			return 0;
		}
		for (i = 0; i < 6; i++)
			if (!strcmp(name, net_names[i])) {
				*out = node(dir->fs, 0, K_NETF + i);
				return *out ? 0 : -ENOMEM;
			}
		return -ENOENT;
	}
	if (!live(pid)) return -ENOENT;
	if (kind == K_PID) {
		if (!strcmp(name, "..")) {
			*out = vref(dir->fs->root);
			return 0;
		}
		for (i = 0; i < NPIDF; i++)
			if (!strcmp(name, pid_names[i])) {
				*out = node(dir->fs, pid, K_STATUS + i);
				return *out ? 0 : -ENOMEM;
			}
		return -ENOENT;
	}
	if (kind == K_FD) {
		Proc *p = live(pid);
		if (!strcmp(name, "..")) {
			*out = node(dir->fs, pid, K_PID);
			return *out ? 0 : -ENOMEM;
		}
		if (is_num(name, &n) && n < NOFILE && p->files[n]) {
			*out = node(dir->fs, pid, K_FDLINK + (unsigned)n);
			return *out ? 0 : -ENOMEM;
		}
	}
	return -ENOENT;
}

static int slot_entry(int pid, unsigned kind, Dirent *de, const char *name) {
	de->inode = ((unsigned)pid << 8) | kind;
	strncpy(de->name, name, 27);
	de->name[27] = 0;
	return 1;
}

static int p_readdir(Vnode *v, unsigned *off, Dirent *de) {
	unsigned kind = KIND(v), o = *off;
	int pid = PID(v);
	char num[16];
	if (o == 0) {
		(*off)++;
		return slot_entry(pid, kind, de, ".");
	}
	if (o == 1) {
		(*off)++;
		return slot_entry(0, K_ROOT, de, "..");
	}
	o -= 2;
	if (kind == K_NET) {
		if (o >= 6) return 0;
		(*off)++;
		return slot_entry(0, K_NETF + o, de, net_names[o]);
	}
	if (kind == K_ROOT) {
		if (o < 8) {
			(*off)++;
			return slot_entry(0, K_MEMINFO + o, de, top_names[o]);
		}
		for (o -= 8; o < NPROC; o++) {
			Proc *p = &procs[o];
			*off = o + 2 + 8 + 1;
			if (p->state == P_UNUSED) continue;
			ksnprintf(num, sizeof(num), "%d", p->pid);
			return slot_entry(p->pid, K_PID, de, num);
		}
		*off = 2 + 8 + NPROC;
		return 0;
	}
	if (!live(pid)) return 0;
	if (kind == K_PID) {
		if (o >= NPIDF) return 0;
		(*off)++;
		return slot_entry(pid, K_STATUS + o, de, pid_names[o]);
	}
	if (kind == K_FD) {
		Proc *p = live(pid);
		for (; o < NOFILE; o++) {
			*off = o + 3;
			if (!p->files[o]) continue;
			ksnprintf(num, sizeof(num), "%d", (int)o);
			return slot_entry(pid, K_FDLINK + o, de, num);
		}
		*off = NOFILE + 2;
	}
	return 0;
}

static int p_getattr(Vnode *v, Stat2 *st) {
	unsigned kind = KIND(v);
	Proc *p = PID(v) ? live(PID(v)) : NULL;
	if (PID(v) && !p) return -ENOENT;
	memset(st, 0, sizeof(*st));
	st->dev = v->fs->dev;
	st->ino = v->ino;
	if (v->type == VT_DIR) {
		st->mode = S_IFDIR | 0555;
		st->nlink = 2;
	} else if (v->type == VT_LNK) {
		st->mode = S_IFLNK | 0777;
		st->nlink = 1;
	} else {
		st->mode = S_IFREG | (kind == K_CTL || kind == K_NOTE || kind == K_NOTEPG ? 0200 : kind == K_ENVIRON || kind == K_FDTAB ? 0400 : 0444);
		st->nlink = 1;
	}
	if (p) {
		st->uid = p->uid;
		st->gid = p->gid;
	}
	st->atime = st->mtime = st->ctime = vfs_now();
	return 0;
}

void fd_name(Proc *p, int fd, char *out, unsigned size) {
	File *f = p->files[fd];
	if (!f) {
		out[0] = 0;
		return;
	}
	if (f->type == FD_PIPE && !f->path) ksnprintf(out, size, "pipe:[%u]", pipe_id(f->pipe));
	else if (f->type == FD_SOCKET && !f->path) ksnprintf(out, size, "socket:[%u]", sock_id(f));
	else if (f->path) strncpy(out, f->path, size);
	else if (f->vn && vnode_path(f->vn, out, size) > 0) return;
	else strncpy(out, "?", size);
}

static int p_readlink(Vnode *v, char *buf, unsigned n) {
	char path[MAXPATH];
	unsigned kind = KIND(v), l;
	Proc *p;
	if (kind == K_SELF) {
		if (!curproc) return -ENOENT;
		ksnprintf(path, sizeof(path), "%d", curproc->pid);
	} else {
		p = live(PID(v));
		if (!p) return -ENOENT;
		if (kind == K_CWD) {
			if (p->cwdpath[0]) strncpy(path, p->cwdpath, sizeof(path) - 1);
			else if (!p->cwd || vnode_path(p->cwd, path, sizeof(path)) < 0) strncpy(path, "?", 2);
		} else if (kind == K_EXE) {
			strncpy(path, p->exe[0] ? p->exe : "?", sizeof(path) - 1);
			path[sizeof(path) - 1] = 0;
		} else if (kind >= K_FDLINK && kind < K_FDLINK + NOFILE) {
			if (!p->files[kind - K_FDLINK]) return -ENOENT;
			fd_name(p, (int)(kind - K_FDLINK), path, sizeof(path));
		} else {
			return -EINVAL;
		}
	}
	l = strlen(path);
	if (l > n) l = n;
	memcpy(buf, path, l);
	return (int)l;
}

static unsigned add(char *b, unsigned at, const char *fmt, unsigned a, unsigned c, unsigned d) {
	if (at >= GENMAX) return at;
	at += (unsigned)ksnprintf(b + at, GENMAX - at, fmt, a, c, d);
	return at > GENMAX ? GENMAX : at;
}

static unsigned adds(char *b, unsigned at, const char *s) {
	unsigned l = strlen(s);
	if (at + l > GENMAX) l = GENMAX - at;
	memcpy(b + at, s, l);
	return at + l;
}

static char state_letter(Proc *p) {
	switch (p->state) {
	case P_RUNNABLE:
	case P_RUNNING:
		return 'R';
	case P_STOPPED:
		return 'T';
	case P_ZOMBIE:
		return 'Z';
	}
	return 'S';
}

static unsigned ignored_mask(Proc *p) {
	unsigned m = 0;
	int i;
	for (i = 1; i < 32; i++)
		if (p->sigact[i].handler == SIG_IGN) m |= 1u << i;
	return m;
}

static unsigned rss_pages(Proc *p) {
	Region *r;
	unsigned n = 0, va;
	if (!p->pgdir) return 0;
	for (r = p->regions; r; r = r->next)
		for (va = r->start; va < r->end; va += PAGE)
			if (walk(p->pgdir, va) & PTE_P) n++;
	return n;
}

static unsigned gen_status(Proc *p, char *b) {
	unsigned at = 0, vm = 0;
	Region *r;
	char st[2];
	for (r = p->regions; r; r = r->next) vm += r->end - r->start;
	at = adds(b, at, "Name:\t");
	at = adds(b, at, p->name);
	st[0] = state_letter(p);
	st[1] = 0;
	at = adds(b, at, "\nState:\t");
	at = adds(b, at, st);
	at = add(b, at, "\nPid:\t%u\nPPid:\t%u\n", (unsigned)p->pid, (unsigned)p->ppid, 0);
	at = add(b, at, "Pgid:\t%u\nSid:\t%u\n", (unsigned)p->pgid, (unsigned)p->sid, 0);
	at = add(b, at, "Uid:\t%u\t%u\n", p->uid, p->euid, 0);
	at = add(b, at, "Gid:\t%u\t%u\n", p->gid, p->egid, 0);
	at = add(b, at, "Nice:\t%d\n", (unsigned)p->nice, 0, 0);
	at = add(b, at, "VmSize:\t%u kB\nVmRSS:\t%u kB\n", vm / 1024, rss_pages(p) * 4, 0);
	at = add(b, at, "Utime:\t%u\nStime:\t%u\n", p->utime, p->stime, 0);
	at = add(b, at, "SigPnd:\t%08x\nSigBlk:\t%08x\nSigIgn:\t%08x\n", p->sigpend, p->sigmask, ignored_mask(p));
	return at;
}

static unsigned gen_maps(Proc *p, char *b) {
	static const char *kinds[] = {"text", "data", "heap", "stack", "mmap"};
	Region *r;
	unsigned at = 0;
	for (r = p->regions; r; r = r->next) {
		char perm[6];
		perm[0] = (r->prot & PROT_READ) ? 'r' : '-';
		perm[1] = (r->prot & PROT_WRITE) ? 'w' : '-';
		perm[2] = (r->prot & PROT_EXEC) ? 'x' : '-';
		perm[3] = (r->flags & MAP_SHARED) ? 's' : 'p';
		perm[4] = ' ';
		perm[5] = 0;
		at = add(b, at, "%08x-%08x ", r->start, r->end, 0);
		at = adds(b, at, perm);
		at = adds(b, at, r->kind >= R_TEXT && r->kind <= R_MMAP ? kinds[r->kind - R_TEXT] : "?");
		at = adds(b, at, "\n");
	}
	return at;
}

static unsigned gen_mounts(char *b) {
	Fs *list[32], *fs;
	int n = 0;
	unsigned at = 0;
	for (fs = mounts; fs && n < 32; fs = fs->next) list[n++] = fs;
	while (n-- > 0) {
		at = adds(b, at, list[n]->source);
		at = adds(b, at, " ");
		at = adds(b, at, list[n]->target);
		at = adds(b, at, " ");
		at = adds(b, at, list[n]->fstype);
		at = adds(b, at, "\n");
	}
	return at;
}

static unsigned gen_loadavg(char *b) {
	unsigned run = 0, total = 0;
	int i;
	for (i = 0; i < NPROC; i++) {
		if (procs[i].state == P_UNUSED) continue;
		total++;
		if (procs[i].state == P_RUNNABLE || procs[i].state == P_RUNNING) run++;
	}
	return add(b, 0, "%u %u %u\n", run, total, (unsigned)last_pid());
}

static unsigned copy_strings(char *b, const char *s, unsigned n) {
	if (n > GENMAX) n = GENMAX;
	if (s && n) memcpy(b, s, n);
	return s ? n : 0;
}

static unsigned gen_fdtab(Proc *p, char *b) {
	char path[MAXPATH];
	unsigned at = 0;
	int i;
	for (i = 0; i < NOFILE; i++) {
		File *f = p->files[i];
		if (!f) continue;
		fd_name(p, i, path, sizeof(path));
		at = add(b, at, "%u ", (unsigned)i, 0, 0);
		at = adds(b, at, f->readable && f->writable ? "rw" : f->writable ? "w" : "r");
		at = add(b, at, " %u ", f->off, 0, 0);
		at = adds(b, at, path);
		at = adds(b, at, "\n");
	}
	return at;
}

static int proc_ctl(Proc *p, const char *c) {
	unsigned v;
	if (!strcmp(c, "kill")) send_signal(p, SIGKILL);
	else if (!strcmp(c, "stop")) send_signal(p, SIGSTOP);
	else if (!strcmp(c, "start")) send_signal(p, SIGCONT);
	else if (!strcmp(c, "hang")) p->hang = 1;
	else if (!strncmp(c, "nice ", 5)) {
		int neg = c[5] == '-', n;
		if (ctl_number(c + 5 + neg, &v) || v > 20) return set_errstr(EINVAL, "bad number in control message", c);
		n = neg ? -(int)v : (int)v;
		if (n < p->nice && curproc->euid) return -EPERM;
		p->nice = n < -20 ? -20 : n > 19 ? 19 : n;
	} else if (!strncmp(c, "close ", 6)) {
		if (ctl_number(c + 6, &v) || v >= NOFILE) return set_errstr(EINVAL, "bad number in control message", c);
		if (!p->files[v]) return -EBADF;
		file_close(p->files[v]);
		p->files[v] = NULL;
		p->fdflags[v] = 0;
	} else return set_errstr(EINVAL, "unknown control message", c);
	return 0;
}

static int p_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	unsigned kind = KIND(v);
	Proc *p = PID(v) ? live(PID(v)) : NULL;
	char cmd[ERRMAX];
	int r;
	(void)off;
	(void)f;
	if (!p || p->state == P_ZOMBIE) return -ESRCH;
	if (kind != K_CTL && kind != K_NOTE && kind != K_NOTEPG) return -EACCES;
	if (!may_signal(p)) return -EPERM;
	r = ctl_text(cmd, buf, n);
	if (r < 0) return r;
	if (kind == K_CTL) r = proc_ctl(p, cmd);
	else if (kind == K_NOTE) post_note(p, cmd);
	else {
		int i, pg = p->pgid;
		for (i = 0; i < NPROC; i++)
			if (procs[i].state != P_UNUSED && procs[i].state != P_ZOMBIE && procs[i].pgid == pg && may_signal(&procs[i])) post_note(&procs[i], cmd);
	}
	return r < 0 ? r : (int)n;
}

static int p_truncate(Vnode *v, unsigned len) {
	(void)v;
	(void)len;
	return 0;
}

static int generate(Vnode *v, char *b) {
	unsigned kind = KIND(v);
	Proc *p = NULL;
	if (PID(v)) {
		p = live(PID(v));
		if (!p) return -ENOENT;
	}
	switch (kind) {
	case K_MEMINFO:
		return (int)add(b, add(b, 0, "MemTotal: %u kB\nMemFree: %u kB\n", frames_total() * 4, frames_free() * 4, 0), "KernelPool: %u kB\nKernelPoolFree: %u kB\n", low_total_pages() * 4, low_free_pages() * 4, 0);
	case K_UPTIME:
		return (int)add(b, 0, "%u %u.%02u\n", ticks, ticks / 100, ticks % 100);
	case K_LOADAVG:
		return (int)gen_loadavg(b);
	case K_MOUNTS:
		return (int)gen_mounts(b);
	case K_VERSION:
		return (int)adds(b, adds(b, adds(b, 0, "TORTOISE 3.0 "), build_date()), "\n");
	case K_CPUINFO:
		return (int)add(b, 0, "cpuid: %u\nram: %u\nticks_per_second: 100\n", rd(0xF0007008u), ram_size, 0);
	case K_NETF:
	case K_NETF + 1:
	case K_NETF + 2:
	case K_NETF + 3:
	case K_NETF + 4:
	case K_NETF + 5:
		return net_proc((int)(kind - K_NETF), b, GENMAX);
	case K_STATUS:
		return (int)gen_status(p, b);
	case K_CMDLINE:
		return (int)copy_strings(b, p->cmdline, p->cmdlen);
	case K_ENVIRON:
		return (int)copy_strings(b, p->environ, p->envlen);
	case K_MAPS:
		return (int)gen_maps(p, b);
	case K_NS:
		return ns_text(p->ns, b, GENMAX);
	case K_CTL:
	case K_NOTE:
	case K_NOTEPG:
		return 0;
	case K_FDTAB:
		return (int)gen_fdtab(p, b);
	case K_ARGS: {
		unsigned i, n = p->cmdlen;
		if (n > GENMAX - 2) n = GENMAX - 2;
		for (i = 0; i < n; i++) b[i] = p->cmdline[i] ? p->cmdline[i] : ' ';
		while (n > 0 && b[n - 1] == ' ') n--;
		b[n++] = '\n';
		return (int)n;
	}
	}
	return -EINVAL;
}

static int p_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	char *b;
	int len;
	(void)f;
	if (v->type != VT_REG) return -EISDIR;
	if (f && off > 0 && f->snap) {
		len = f->snaplen;
		if (off >= (unsigned)len) return 0;
		if (n > (unsigned)len - off) n = (unsigned)len - off;
		memcpy(buf, f->snap + off, n);
		return (int)n;
	}
	b = (char *)low_alloc_n(GENMAX / PAGE);
	if (!b) return -ENOMEM;
	len = generate(v, b);
	if (len < 0) {
		low_free_n((unsigned)b, GENMAX / PAGE);
		return len;
	}
	if (f) {
		kfree(f->snap);
		f->snap = kmalloc((unsigned)len + 1);
		f->snaplen = f->snap ? len : 0;
		if (f->snap) memcpy(f->snap, b, (unsigned)len);
	}
	if (off >= (unsigned)len) n = 0;
	else if (n > (unsigned)len - off) n = (unsigned)len - off;
	memcpy(buf, b + off, n);
	low_free_n((unsigned)b, GENMAX / PAGE);
	return (int)n;
}

FsOps procfs_ops = {
	p_lookup, NULL, NULL, NULL, NULL, NULL, NULL, NULL, p_readlink,
	p_read, p_write, p_readdir, p_getattr, NULL, p_truncate, NULL, NULL, NULL, NULL
};

int procfs_mount(Fs *fs) {
	fs->ops = &procfs_ops;
	fs->dev = (4u << 8) | procfs_dev++;
	fs->root = vget(fs, K_ROOT, VT_DIR);
	return fs->root ? 0 : -ENOMEM;
}
