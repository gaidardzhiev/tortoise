/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"
#include "net.h"

#define T_DIR 1
#define T_ALIAS 2
#define NPDEV 16

typedef struct Node9 {
	unsigned ino, parent;
	const char *name;
	int kind, arg;
} Node9;

typedef struct Tab9 {
	const Node9 *n;
	int count;
} Tab9;

typedef struct PipeDev {
	int used;
	Pipe *ab, *ba;
	int opened[2];
} PipeDev;

static const Node9 disk_nodes[] = {
	{1, 1, "", T_DIR, 0},
	{2, 1, "disk0", T_ALIAS, 0},
	{3, 1, "disk1", T_ALIAS, 0},
};

static Tab9 disk_tab = {disk_nodes, 3};
static Fs disk_fs, pipe_fs;
static Vnode *cons_root, *proc_root;
static PipeDev pdevs[NPDEV];
static unsigned dev9_dev;
extern FsOps tab9_ops, pipe9_ops;

int set_errstr(int err, const char *msg, const char *arg) {
	if (curproc) {
		if (arg) ksnprintf(curproc->errstr, ERRMAX, "%s '%s'", msg, arg);
		else ksnprintf(curproc->errstr, ERRMAX, "%s", msg);
		curproc->errset = 1;
	}
	return -err;
}

int sys_errstr(unsigned ubuf, unsigned n) {
	char tmp[ERRMAX];
	unsigned l;
	int r;
	if (n == 0) return -EINVAL;
	r = copyinstr(tmp, ubuf, n < ERRMAX ? n : ERRMAX);
	if (r < 0) {
		if (r != -EINVAL) return r;
		tmp[(n < ERRMAX ? n : ERRMAX) - 1] = 0;
	}
	l = strlen(curproc->errstr);
	if (l >= n) l = n - 1;
	if (copyout(ubuf, curproc->errstr, l)) return -EFAULT;
	if (copyout(ubuf + l, "", 1)) return -EFAULT;
	strncpy(curproc->errstr, tmp, ERRMAX);
	curproc->errstr[ERRMAX - 1] = 0;
	return 0;
}

int ctl_text(char *dst, const char *src, unsigned n) {
	while (n > 0 && (src[n - 1] == '\n' || src[n - 1] == ' ')) n--;
	if (n >= ERRMAX) return set_errstr(EINVAL, "control message too long", NULL);
	memcpy(dst, src, n);
	dst[n] = 0;
	return (int)n;
}

int ctl_number(const char *s, unsigned *out) {
	unsigned v = 0;
	if (*s < '0' || *s > '9') return -1;
	for (; *s >= '0' && *s <= '9'; s++) {
		if (v > 100000000) return -1;
		v = v * 10 + (unsigned)(*s - '0');
	}
	if (*s) return -1;
	*out = v;
	return 0;
}

int ctl_addr(const char *s, const char **end, unsigned *out) {
	unsigned a = 0, part;
	int i;
	for (i = 0; i < 4; i++) {
		if (*s < '0' || *s > '9') return -1;
		for (part = 0; *s >= '0' && *s <= '9'; s++) {
			part = part * 10 + (unsigned)(*s - '0');
			if (part > 255) return -1;
		}
		a = (a << 8) | part;
		if (i < 3 && *s++ != '.') return -1;
	}
	if (*s && *s != ' ') return -1;
	while (*s == ' ') s++;
	*out = a;
	*end = s;
	return 0;
}

static void tab_init(Fs *fs, Tab9 *t, const char *name) {
	memset(fs, 0, sizeof(Fs));
	fs->ops = &tab9_ops;
	fs->priv = t;
	fs->dev = (40u << 8) | dev9_dev++;
	strncpy(fs->source, name, 63);
	strncpy(fs->fstype, "dev9", 7);
	fs->root = vget(fs, 1, VT_DIR);
}

void dev9_init(void) {
	vfs_resolve("/dev", rootvn, 1, &cons_root);
	vfs_resolve("/proc", rootvn, 1, &proc_root);
	tab_init(&disk_fs, &disk_tab, "#D");
	netfs_init();
	memset(&pipe_fs, 0, sizeof(Fs));
	pipe_fs.ops = &pipe9_ops;
	pipe_fs.dev = (40u << 8) | dev9_dev++;
	strncpy(pipe_fs.source, "#|", 63);
	strncpy(pipe_fs.fstype, "dev9", 7);
}

static Vnode *pipe_attach(void) {
	int i;
	for (i = 0; i < NPDEV; i++)
		if (!pdevs[i].used) {
			PipeDev *d = &pdevs[i];
			d->ab = pipe_alloc();
			d->ba = d->ab ? pipe_alloc() : NULL;
			if (!d->ba) {
				if (d->ab) d->ab->used = 0;
				return NULL;
			}
			d->used = 1;
			d->opened[0] = d->opened[1] = 0;
			d->ab->readers = d->ab->writers = 1;
			d->ba->readers = d->ba->writers = 1;
			return vget(&pipe_fs, (unsigned)i * 4 + 1, VT_DIR);
		}
	return NULL;
}

Vnode *dev9_root(int c) {
	switch (c) {
	case 'c':
		return cons_root ? vref(cons_root) : NULL;
	case 'p':
		return proc_root ? vref(proc_root) : NULL;
	case 'D':
		return vref(disk_fs.root);
	case 'l':
	case 'I':
		return netfs_root(c);
	case '|':
		return pipe_attach();
	case 's':
		return srv_root();
	case 'e':
		return env_root();
	}
	return NULL;
}

static const Node9 *node_of(Vnode *v) {
	Tab9 *t = v->fs->priv;
	int i;
	for (i = 0; i < t->count; i++)
		if (t->n[i].ino == v->ino) return &t->n[i];
	return NULL;
}

static int t_lookup(Vnode *dir, const char *name, Vnode **out) {
	Tab9 *t = dir->fs->priv;
	const Node9 *d = node_of(dir);
	int i;
	if (!d || d->kind != T_DIR) return -ENOTDIR;
	if (!strcmp(name, ".") || !strcmp(name, "..")) {
		*out = vget(dir->fs, !strcmp(name, ".") ? dir->ino : d->parent, VT_DIR);
		return *out ? 0 : -ENOMEM;
	}
	for (i = 0; i < t->count; i++)
		if (t->n[i].parent == dir->ino && t->n[i].ino != dir->ino && !strcmp(t->n[i].name, name)) {
			if (t->n[i].kind == T_ALIAS) return cons_root->fs->ops->lookup(cons_root, name, out);
			*out = vget(dir->fs, t->n[i].ino, t->n[i].kind == T_DIR ? VT_DIR : VT_REG);
			return *out ? 0 : -ENOMEM;
		}
	return -ENOENT;
}

static int t_readdir(Vnode *v, unsigned *off, Dirent *de) {
	Tab9 *t = v->fs->priv;
	const Node9 *d = node_of(v);
	unsigned k = 0;
	int i;
	if (*off < 2) {
		de->inode = *off == 0 ? v->ino : d->parent;
		strncpy(de->name, *off == 0 ? "." : "..", 28);
		(*off)++;
		return 1;
	}
	for (i = 0; i < t->count; i++)
		if (t->n[i].parent == v->ino && t->n[i].ino != v->ino) {
			if (k++ == *off - 2) {
				de->inode = t->n[i].ino;
				strncpy(de->name, t->n[i].name, 28);
				(*off)++;
				return 1;
			}
		}
	return 0;
}

static int t_getattr(Vnode *v, Stat2 *st) {
	const Node9 *d = node_of(v);
	if (!d) return -ENOENT;
	st->dev = v->fs->dev;
	st->ino = v->ino;
	st->nlink = d->kind == T_DIR ? 2 : 1;
	st->mode = d->kind == T_DIR ? S_IFDIR | 0555 : S_IFREG | 0644;
	st->atime = st->mtime = st->ctime = vfs_now();
	return 0;
}

static int t_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	const Node9 *d = node_of(v);
	char *b;
	int len;
	(void)f;
	if (!d || d->kind == T_DIR) return -EISDIR;
	b = kmalloc(4096);
	if (!b) return -ENOMEM;
	len = 0;
	if (len < 0) len = 0;
	if (off >= (unsigned)len) n = 0;
	else if (n > (unsigned)len - off) n = (unsigned)len - off;
	memcpy(buf, b + off, n);
	kfree(b);
	return (int)n;
}

static int t_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	const Node9 *d = node_of(v);
	char cmd[ERRMAX];
	int r;
	(void)off;
	(void)f;
	if (!d || d->kind == T_DIR) return -EISDIR;
	r = ctl_text(cmd, buf, n);
	if (r < 0) return r;
	r = -EACCES;
	return r < 0 ? r : (int)n;
}

static int t_truncate(Vnode *v, unsigned len) {
	(void)v;
	(void)len;
	return 0;
}

FsOps tab9_ops = {
	t_lookup, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	t_read, t_write, t_readdir, t_getattr, NULL, t_truncate, NULL, NULL, NULL, NULL
};

static PipeDev *pdev_of(Vnode *v) {
	unsigned k = v->ino / 4;
	return k < NPDEV && pdevs[k].used ? &pdevs[k] : NULL;
}

static int pp_lookup(Vnode *dir, const char *name, Vnode **out) {
	unsigned base = dir->ino & ~3u;
	if (!pdev_of(dir)) return -ENOENT;
	if (!strcmp(name, ".") || !strcmp(name, "..")) *out = vref(dir);
	else if (!strcmp(name, "data")) *out = vget(dir->fs, base + 2, VT_FIFO);
	else if (!strcmp(name, "data1")) *out = vget(dir->fs, base + 3, VT_FIFO);
	else return -ENOENT;
	return *out ? 0 : -ENOMEM;
}

static int pp_readdir(Vnode *v, unsigned *off, Dirent *de) {
	static const char *names[] = {".", "..", "data", "data1"};
	static const unsigned ino[] = {1, 1, 2, 3};
	if (*off >= 4) return 0;
	de->inode = (v->ino & ~3u) + ino[*off];
	strncpy(de->name, names[*off], 28);
	(*off)++;
	return 1;
}

static int pp_getattr(Vnode *v, Stat2 *st) {
	PipeDev *d = pdev_of(v);
	unsigned e = v->ino & 3;
	st->dev = v->fs->dev;
	st->ino = v->ino;
	st->nlink = e == 1 ? 2 : 1;
	st->mode = e == 1 ? S_IFDIR | 0500 : S_IFIFO | 0600;
	if (e == 2 && d) st->size = d->ba->nwrite - d->ba->nread;
	if (e == 3 && d) st->size = d->ab->nwrite - d->ab->nread;
	if (curproc) {
		st->uid = curproc->euid;
		st->gid = curproc->egid;
	}
	st->atime = st->mtime = st->ctime = vfs_now();
	return 0;
}

static int alive(Vnode *gone, unsigned base) {
	unsigned i;
	for (i = 1; i <= 3; i++) {
		Vnode *v = vpeek(&pipe_fs, base + i);
		if (v && v != gone) return 1;
	}
	return 0;
}

static void pp_release(Vnode *v) {
	PipeDev *d = pdev_of(v);
	if (!d || alive(v, v->ino & ~3u)) return;
	if (!d->opened[0]) {
		d->ab->writers--;
		d->ba->readers--;
	}
	if (!d->opened[1]) {
		d->ba->writers--;
		d->ab->readers--;
	}
	pipe_check(d->ab);
	pipe_check(d->ba);
	d->used = 0;
}

FsOps pipe9_ops = {
	pp_lookup, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, pp_readdir, pp_getattr, NULL, NULL, NULL, NULL, pp_release, NULL
};

int pipe9_open(Vnode *v, File *f) {
	PipeDev *d = pdev_of(v);
	unsigned e = v->ino & 3;
	Pipe *rd, *wr;
	if (!d) return -ENOENT;
	if (e == 1) return 0;
	rd = e == 2 ? d->ba : d->ab;
	wr = e == 2 ? d->ab : d->ba;
	if (!d->opened[e - 2]) {
		d->opened[e - 2] = 1;
		wr->writers--;
		rd->readers--;
	}
	if (f->readable) rd->readers++;
	if (f->writable) wr->writers++;
	f->type = FD_PIPE;
	f->pipe = rd;
	f->pipe2 = wr;
	vput(f->vn);
	f->vn = NULL;
	return 0;
}

int dev9_is_pipe(Vnode *v) {
	return v->fs->ops == &pipe9_ops && (v->ino & 3) >= 2;
}
