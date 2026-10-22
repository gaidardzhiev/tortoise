/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define NSRV 32

typedef struct Srv {
	int used;
	char name[28];
	File *f;
	unsigned mode, uid, gid, mtime;
} Srv;

static Srv srvs[NSRV];
static Fs srv_fs;
extern FsOps srv_ops;

static Srv *srv_of(Vnode *v) {
	unsigned i = v->ino - 2;
	return v->ino >= 2 && i < NSRV && srvs[i].used ? &srvs[i] : NULL;
}

static int s_lookup(Vnode *dir, const char *name, Vnode **out) {
	int i;
	if (dir->ino != 1) return -ENOTDIR;
	if (!strcmp(name, ".") || !strcmp(name, "..")) {
		*out = vref(dir);
		return 0;
	}
	for (i = 0; i < NSRV; i++)
		if (srvs[i].used && !strcmp(srvs[i].name, name)) {
			*out = vget(dir->fs, (unsigned)i + 2, VT_REG);
			return *out ? 0 : -ENOMEM;
		}
	return -ENOENT;
}

static int s_create(Vnode *dir, const char *name, unsigned mode, Vnode **out) {
	int i, slot = -1;
	if (dir->ino != 1) return -ENOTDIR;
	for (i = 0; i < NSRV; i++) {
		if (srvs[i].used && !strcmp(srvs[i].name, name)) return -EEXIST;
		if (!srvs[i].used && slot < 0) slot = i;
	}
	if (slot < 0) return -ENOSPC;
	memset(&srvs[slot], 0, sizeof(Srv));
	srvs[slot].used = 1;
	strncpy(srvs[slot].name, name, 27);
	srvs[slot].mode = mode & 0777;
	srvs[slot].uid = curproc->euid;
	srvs[slot].gid = curproc->egid;
	srvs[slot].mtime = vfs_now();
	*out = vget(dir->fs, (unsigned)slot + 2, VT_REG);
	return *out ? 0 : -ENOMEM;
}

static int s_unlink(Vnode *dir, const char *name) {
	int i;
	(void)dir;
	for (i = 0; i < NSRV; i++)
		if (srvs[i].used && !strcmp(srvs[i].name, name)) {
			if (curproc->euid && curproc->euid != srvs[i].uid) return -EPERM;
			if (srvs[i].f) file_close(srvs[i].f);
			srvs[i].f = NULL;
			srvs[i].used = 0;
			return 0;
		}
	return -ENOENT;
}

static int s_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	Srv *s = srv_of(v);
	char cmd[ERRMAX];
	unsigned fd;
	int r;
	(void)off;
	(void)f;
	if (!s) return -ENOENT;
	if (s->f) return set_errstr(EEXIST, "already posted", s->name);
	r = ctl_text(cmd, buf, n);
	if (r < 0) return r;
	if (ctl_number(cmd, &fd) || fd >= NOFILE || !curproc->files[fd]) return set_errstr(EBADF, "bad descriptor to post", cmd);
	if (curproc->files[fd]->type == FD_INODE && curproc->files[fd]->vn == v) return -EINVAL;
	s->f = file_dup(curproc->files[fd]);
	return (int)n;
}

static int s_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	(void)v;
	(void)buf;
	(void)off;
	(void)n;
	(void)f;
	return 0;
}

static int s_readdir(Vnode *v, unsigned *off, Dirent *de) {
	if (v->ino != 1) return -ENOTDIR;
	if (*off < 2) {
		de->inode = 1;
		strncpy(de->name, *off ? ".." : ".", 28);
		(*off)++;
		return 1;
	}
	while (*off - 2 < NSRV) {
		unsigned i = *off - 2;
		(*off)++;
		if (srvs[i].used) {
			de->inode = i + 2;
			strncpy(de->name, srvs[i].name, 28);
			return 1;
		}
	}
	return 0;
}

static int s_getattr(Vnode *v, Stat2 *st) {
	Srv *s = srv_of(v);
	st->dev = v->fs->dev;
	st->ino = v->ino;
	if (v->ino == 1) {
		st->mode = S_IFDIR | 0777;
		st->nlink = 2;
		st->atime = st->mtime = st->ctime = vfs_now();
		return 0;
	}
	if (!s) return -ENOENT;
	st->mode = S_IFREG | s->mode;
	st->nlink = 1;
	st->uid = s->uid;
	st->gid = s->gid;
	st->atime = st->mtime = st->ctime = s->mtime;
	return 0;
}

static int s_setattr(Vnode *v, int what, unsigned a, unsigned b) {
	Srv *s = srv_of(v);
	if (!s) return -EPERM;
	if (what == SA_MODE) s->mode = a & 0777;
	else if (what == SA_OWNER) {
		if (a != 0xFFFFFFFFu) s->uid = a;
		if (b != 0xFFFFFFFFu) s->gid = b;
	}
	return 0;
}

static int s_truncate(Vnode *v, unsigned len) {
	(void)v;
	(void)len;
	return 0;
}

FsOps srv_ops = {
	s_lookup, s_create, NULL, s_unlink, NULL, NULL, NULL, NULL, NULL,
	s_read, s_write, s_readdir, s_getattr, s_setattr, s_truncate, NULL, NULL, NULL, NULL
};

void srv_init(void) {
	memset(&srv_fs, 0, sizeof(Fs));
	srv_fs.ops = &srv_ops;
	srv_fs.dev = (42u << 8);
	strncpy(srv_fs.source, "#s", 63);
	strncpy(srv_fs.fstype, "srv", 7);
	srv_fs.root = vget(&srv_fs, 1, VT_DIR);
}

Vnode *srv_root(void) {
	return vref(srv_fs.root);
}

File *srv_posted(Vnode *v) {
	Srv *s;
	if (v->fs != &srv_fs) return NULL;
	s = srv_of(v);
	return s ? s->f : NULL;
}

const char *srv_name_of(File *f) {
	int i;
	for (i = 0; i < NSRV; i++)
		if (srvs[i].used && srvs[i].f == f) return srvs[i].name;
	return NULL;
}
