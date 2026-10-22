/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define NEVAR 64
#define MAXEVAL 16384

typedef struct Evar {
	int used;
	char name[28];
	char *val;
	unsigned len;
	unsigned path, uid, gid, mode, mtime;
} Evar;

typedef struct Egrp {
	int ref;
	unsigned id;
	Evar v[NEVAR];
	struct Egrp *next;
} Egrp;

static Egrp bootgrp;
static Egrp *grps = &bootgrp;
static unsigned nextid = 1, nextpath = 1;
static Fs env_fs;
extern FsOps env_ops;

Egrp *egrp_boot(void) {
	if (!bootgrp.ref) {
		bootgrp.ref = 1;
		bootgrp.id = nextid++;
	}
	return &bootgrp;
}

Egrp *egrp_hold(Egrp *g) {
	g->ref++;
	return g;
}

static Egrp *egrp_alloc(void) {
	Egrp *g = kmalloc(sizeof(Egrp));
	if (!g) return NULL;
	memset(g, 0, sizeof(Egrp));
	g->ref = 1;
	g->id = nextid++;
	g->next = grps;
	grps = g;
	return g;
}

Egrp *egrp_clean(void) {
	return egrp_alloc();
}

Egrp *egrp_copy(Egrp *src) {
	Egrp *g = egrp_alloc();
	int i;
	if (!g) return NULL;
	for (i = 0; i < NEVAR; i++)
		if (src->v[i].used) {
			g->v[i] = src->v[i];
			g->v[i].path = nextpath++;
			g->v[i].val = NULL;
			if (src->v[i].len) {
				g->v[i].val = kmalloc(src->v[i].len);
				if (!g->v[i].val) {
					g->v[i].used = 0;
					continue;
				}
				memcpy(g->v[i].val, src->v[i].val, src->v[i].len);
			}
		}
	return g;
}

void egrp_put(Egrp *g) {
	Egrp **pp;
	int i;
	if (!g || --g->ref > 0 || g == &bootgrp) return;
	for (i = 0; i < NEVAR; i++)
		if (g->v[i].used) kfree(g->v[i].val);
	for (pp = &grps; *pp; pp = &(*pp)->next)
		if (*pp == g) {
			*pp = g->next;
			break;
		}
	kfree(g);
}

static Egrp *cur(void) {
	return curproc && curproc->egrp ? curproc->egrp : egrp_boot();
}

static Egrp *grp_of(Vnode *v) {
	Egrp *g;
	unsigned id = v->ino >> 8;
	if (v->ino == 1) return cur();
	for (g = grps; g; g = g->next)
		if (g->id == id) return g;
	return NULL;
}

static Evar *var_of(Vnode *v) {
	Egrp *g = grp_of(v);
	unsigned i = (v->ino & 0xFF) - 2;
	if (!g || v->ino == 1 || i >= NEVAR || !g->v[i].used) return NULL;
	return &g->v[i];
}

static int e_lookup(Vnode *dir, const char *name, Vnode **out) {
	Egrp *g = cur();
	int i;
	if (dir->ino != 1) return -ENOTDIR;
	if (!strcmp(name, ".") || !strcmp(name, "..")) {
		*out = vref(dir);
		return 0;
	}
	for (i = 0; i < NEVAR; i++)
		if (g->v[i].used && !strcmp(g->v[i].name, name)) {
			*out = vget(dir->fs, (g->id << 8) | (unsigned)(i + 2), VT_REG);
			return *out ? 0 : -ENOMEM;
		}
	return -ENOENT;
}

static int e_create(Vnode *dir, const char *name, unsigned mode, Vnode **out) {
	Egrp *g = cur();
	int i, slot = -1;
	if (dir->ino != 1) return -ENOTDIR;
	for (i = 0; i < NEVAR; i++) {
		if (g->v[i].used && !strcmp(g->v[i].name, name)) return -EEXIST;
		if (!g->v[i].used && slot < 0) slot = i;
	}
	if (slot < 0) return set_errstr(ENOSPC, "environment is full", NULL);
	memset(&g->v[slot], 0, sizeof(Evar));
	g->v[slot].used = 1;
	strncpy(g->v[slot].name, name, 27);
	g->v[slot].path = nextpath++;
	g->v[slot].mode = mode & 0777;
	g->v[slot].uid = curproc->euid;
	g->v[slot].gid = curproc->egid;
	g->v[slot].mtime = vfs_now();
	*out = vget(dir->fs, (g->id << 8) | (unsigned)(slot + 2), VT_REG);
	return *out ? 0 : -ENOMEM;
}

static int e_unlink(Vnode *dir, const char *name) {
	Egrp *g = cur();
	int i;
	(void)dir;
	for (i = 0; i < NEVAR; i++)
		if (g->v[i].used && !strcmp(g->v[i].name, name)) {
			kfree(g->v[i].val);
			memset(&g->v[i], 0, sizeof(Evar));
			return 0;
		}
	return -ENOENT;
}

static int e_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	Evar *e = var_of(v);
	(void)f;
	if (v->ino == 1) return -EISDIR;
	if (!e) return -ENOENT;
	if (off >= e->len) return 0;
	if (n > e->len - off) n = e->len - off;
	memcpy(buf, e->val + off, n);
	return (int)n;
}

static int resize(Evar *e, unsigned len) {
	char *nv;
	if (len > MAXEVAL) return set_errstr(EFBIG, "environment value too long", NULL);
	if (len == e->len) return 0;
	nv = len ? kmalloc(len) : NULL;
	if (len && !nv) return -ENOMEM;
	if (len) {
		memset(nv, 0, len);
		memcpy(nv, e->val, e->len < len ? e->len : len);
	}
	kfree(e->val);
	e->val = nv;
	e->len = len;
	return 0;
}

static int e_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	Evar *e = var_of(v);
	int r;
	(void)f;
	if (!e) return -ENOENT;
	if (off + n > e->len && (r = resize(e, off + n)) != 0) return r;
	memcpy(e->val + off, buf, n);
	e->mtime = vfs_now();
	return (int)n;
}

static int e_truncate(Vnode *v, unsigned len) {
	Evar *e = var_of(v);
	if (!e) return v->ino == 1 ? -EISDIR : -ENOENT;
	return resize(e, len);
}

static int e_readdir(Vnode *v, unsigned *off, Dirent *de) {
	Egrp *g = cur();
	if (v->ino != 1) return -ENOTDIR;
	if (*off < 2) {
		de->inode = 1;
		strncpy(de->name, *off ? ".." : ".", 28);
		(*off)++;
		return 1;
	}
	while (*off - 2 < NEVAR) {
		unsigned i = *off - 2;
		(*off)++;
		if (g->v[i].used) {
			de->inode = (g->id << 8) | (i + 2);
			strncpy(de->name, g->v[i].name, 28);
			return 1;
		}
	}
	return 0;
}

static int e_getattr(Vnode *v, Stat2 *st) {
	Evar *e;
	st->dev = v->fs->dev;
	st->ino = v->ino;
	if (v->ino == 1) {
		st->mode = S_IFDIR | 0777;
		st->nlink = 2;
		st->atime = st->mtime = st->ctime = vfs_now();
		return 0;
	}
	e = var_of(v);
	if (!e) return -ENOENT;
	st->mode = S_IFREG | e->mode;
	st->nlink = 1;
	st->uid = e->uid;
	st->gid = e->gid;
	st->size = e->len;
	st->atime = st->mtime = st->ctime = e->mtime;
	return 0;
}

static int e_setattr(Vnode *v, int what, unsigned a, unsigned b) {
	Evar *e = var_of(v);
	if (!e) return -EPERM;
	if (what == SA_MODE) e->mode = a & 0777;
	else if (what == SA_OWNER) {
		if (a != 0xFFFFFFFFu) e->uid = a;
		if (b != 0xFFFFFFFFu) e->gid = b;
	} else if (what == SA_TIMES) e->mtime = b;
	return 0;
}

FsOps env_ops = {
	e_lookup, e_create, NULL, e_unlink, NULL, NULL, NULL, NULL, NULL,
	e_read, e_write, e_readdir, e_getattr, e_setattr, e_truncate, NULL, NULL, NULL, NULL
};

void env_init(void) {
	memset(&env_fs, 0, sizeof(Fs));
	env_fs.ops = &env_ops;
	env_fs.dev = (43u << 8);
	strncpy(env_fs.source, "#e", 63);
	strncpy(env_fs.fstype, "env", 7);
	env_fs.root = vget(&env_fs, 1, VT_DIR);
	egrp_boot();
}

Vnode *env_root(void) {
	return vref(env_fs.root);
}
