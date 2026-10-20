#include "kernel.h"

typedef struct RNode RNode;
typedef struct REnt REnt;

struct REnt {
	char name[28];
	RNode *node;
	REnt *next;
};

struct RNode {
	int type;
	unsigned ino, mode, uid, gid, nlink, size, atime, mtime, ctime;
	unsigned *pages;
	unsigned npages;
	REnt *ents;
	RNode *parent;
	int open;
};

typedef struct {
	unsigned next_ino;
	unsigned used, limit;
	RNode *root;
} Ram;

static Ram *R(Vnode *v) {
	return v->fs->priv;
}

static RNode *N(Vnode *v) {
	return v->priv;
}

static Vnode *vnode_of(Fs *fs, RNode *n) {
	Vnode *v = vget(fs, n->ino, n->type);
	if (v) v->priv = n;
	return v;
}

static RNode *new_node(Ram *r, int type, unsigned mode) {
	RNode *n = kmalloc(sizeof(RNode));
	unsigned now = vfs_now();
	if (!n) return NULL;
	n->type = type;
	n->ino = r->next_ino++;
	n->mode = mode & 07777;
	n->uid = curproc ? curproc->euid : 0;
	n->gid = curproc ? curproc->egid : 0;
	n->nlink = type == VT_DIR ? 2 : 1;
	n->atime = n->mtime = n->ctime = now;
	return n;
}

static REnt *find_ent(RNode *d, const char *name, REnt ***pp) {
	REnt **e;
	for (e = &d->ents; *e; e = &(*e)->next)
		if (!strncmp((*e)->name, name, 27)) {
			if (pp) *pp = e;
			return *e;
		}
	return NULL;
}

static int add_ent(RNode *d, const char *name, RNode *n) {
	REnt *e, **pp = &d->ents;
	if (strlen(name) > 27) return -ENAMETOOLONG;
	e = kmalloc(sizeof(REnt));
	if (!e) return -ENOMEM;
	strncpy(e->name, name, 27);
	e->node = n;
	while (*pp) pp = &(*pp)->next;
	*pp = e;
	d->mtime = d->ctime = vfs_now();
	return 0;
}

static void free_data(Ram *r, RNode *n, unsigned keep) {
	unsigned i;
	for (i = keep; i < n->npages; i++)
		if (n->pages[i]) {
			frame_free(n->pages[i]);
			n->pages[i] = 0;
			r->used--;
		}
}

static void maybe_free(Ram *r, RNode *n, Fs *fs) {
	Vnode *v;
	if (n->nlink > 0) return;
	v = vget(fs, n->ino, n->type);
	if (v && v->ref > 1) {
		v->priv = n;
		v->nlink_zero = 1;
		vput(v);
		return;
	}
	if (v) {
		v->nlink_zero = 0;
		vput(v);
	}
	free_data(r, n, 0);
	kfree(n->pages);
	kfree(n);
}

static int r_lookup(Vnode *dir, const char *name, Vnode **out) {
	RNode *d = N(dir);
	REnt *e;
	if (!strcmp(name, "..")) {
		*out = vnode_of(dir->fs, d->parent ? d->parent : d);
		return *out ? 0 : -ENOMEM;
	}
	e = find_ent(d, name, NULL);
	if (!e) return -ENOENT;
	*out = vnode_of(dir->fs, e->node);
	return *out ? 0 : -ENOMEM;
}

static int make(Vnode *dir, const char *name, int type, unsigned mode, RNode **out) {
	Ram *r = R(dir);
	RNode *d = N(dir), *n;
	int e;
	if (find_ent(d, name, NULL)) return -EEXIST;
	n = new_node(r, type, mode);
	if (!n) return -ENOMEM;
	if (d->mode & 02000) {
		n->gid = d->gid;
		if (type == VT_DIR) n->mode |= 02000;
	}
	n->parent = d;
	e = add_ent(d, name, n);
	if (e) {
		kfree(n);
		return e;
	}
	*out = n;
	return 0;
}

static int r_create(Vnode *dir, const char *name, unsigned mode, Vnode **out) {
	RNode *n;
	int e = make(dir, name, VT_REG, mode, &n);
	if (e) return e;
	*out = vnode_of(dir->fs, n);
	return *out ? 0 : -ENOMEM;
}

static int r_mknod(Vnode *dir, const char *name, unsigned mode, Vnode **out) {
	RNode *n;
	int t = (mode & S_IFMT) == S_IFSOCK ? VT_SOCK : VT_FIFO;
	int e = make(dir, name, t, mode, &n);
	if (e) return e;
	*out = vnode_of(dir->fs, n);
	return *out ? 0 : -ENOMEM;
}

static int r_mkdir(Vnode *dir, const char *name, unsigned mode) {
	RNode *n;
	int e = make(dir, name, VT_DIR, mode, &n);
	if (e) return e;
	N(dir)->nlink++;
	return 0;
}

static int remove_ent(Vnode *dir, const char *name, int wantdir) {
	RNode *d = N(dir), *n;
	REnt **pp, *e = find_ent(d, name, &pp);
	if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
	if (!e) return -ENOENT;
	n = e->node;
	if (wantdir && n->type != VT_DIR) return -ENOTDIR;
	if (!wantdir && n->type == VT_DIR) return -EISDIR;
	if (wantdir && n->ents) return -ENOTEMPTY;
	*pp = e->next;
	kfree(e);
	d->mtime = d->ctime = vfs_now();
	if (wantdir) {
		d->nlink--;
		n->nlink = 0;
	} else {
		n->nlink--;
	}
	n->ctime = vfs_now();
	maybe_free(R(dir), n, dir->fs);
	return 0;
}

static int r_unlink(Vnode *dir, const char *name) {
	return remove_ent(dir, name, 0);
}

static int r_rmdir(Vnode *dir, const char *name) {
	return remove_ent(dir, name, 1);
}

static int r_rename(Vnode *odir, const char *on, Vnode *ndir, const char *nn) {
	RNode *od = N(odir), *nd = N(ndir), *n;
	REnt **pp, *e = find_ent(od, on, &pp), *t;
	if (!e) return -ENOENT;
	n = e->node;
	t = find_ent(nd, nn, NULL);
	if (t && t->node == n) return 0;
	if (t) {
		int r;
		if (t->node->type == VT_DIR) {
			if (n->type != VT_DIR) return -EISDIR;
			r = r_rmdir(ndir, nn);
		} else {
			if (n->type == VT_DIR) return -ENOTDIR;
			r = r_unlink(ndir, nn);
		}
		if (r) return r;
		e = find_ent(od, on, &pp);
	}
	if (strlen(nn) > 27) return -ENAMETOOLONG;
	*pp = e->next;
	strncpy(e->name, nn, 27);
	e->name[27] = 0;
	e->next = NULL;
	pp = &nd->ents;
	while (*pp) pp = &(*pp)->next;
	*pp = e;
	if (n->type == VT_DIR && od != nd) {
		od->nlink--;
		nd->nlink++;
		n->parent = nd;
	}
	n->ctime = vfs_now();
	return 0;
}

static int r_link(Vnode *dir, const char *name, Vnode *target) {
	RNode *n = N(target);
	int e;
	if (n->type == VT_DIR) return -EPERM;
	if (find_ent(N(dir), name, NULL)) return -EEXIST;
	e = add_ent(N(dir), name, n);
	if (e) return e;
	n->nlink++;
	n->ctime = vfs_now();
	return 0;
}

static int r_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	RNode *x = N(v);
	unsigned done = 0;
	(void)f;
	if (off >= x->size) return 0;
	if (off + n > x->size || off + n < off) n = x->size - off;
	while (done < n) {
		unsigned pg = off / PAGE, o = off % PAGE, k = PAGE - o;
		if (k > n - done) k = n - done;
		if (pg < x->npages && x->pages[pg]) memcpy(buf + done, (char *)kmap(3, x->pages[pg]) + o, k);
		else memset(buf + done, 0, k);
		done += k;
		off += k;
	}
	return (int)done;
}

static int grow_pages(RNode *x, unsigned np) {
	unsigned *nps, cap = 8;
	if (np <= x->npages) return 0;
	while (cap < np) cap *= 2;
	nps = kmalloc(cap * 4);
	if (!nps) return -ENOMEM;
	if (x->pages) {
		memcpy(nps, x->pages, x->npages * 4);
		kfree(x->pages);
	}
	x->pages = nps;
	x->npages = cap;
	return 0;
}

static int r_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	Ram *r = R(v);
	RNode *x = N(v);
	unsigned done = 0;
	(void)f;
	if (off + n < off) return -EINVAL;
	if (grow_pages(x, (off + n + PAGE - 1) / PAGE)) return -ENOSPC;
	while (done < n) {
		unsigned pg = off / PAGE, o = off % PAGE, k = PAGE - o;
		if (k > n - done) k = n - done;
		if (!x->pages[pg]) {
			unsigned pa;
			if (r->used >= r->limit || !(pa = frame_alloc())) break;
			memset(kmap(3, pa), 0, PAGE);
			x->pages[pg] = pa;
			r->used++;
		}
		memcpy((char *)kmap(3, x->pages[pg]) + o, buf + done, k);
		done += k;
		off += k;
	}
	if (off > x->size) x->size = off;
	x->mtime = x->ctime = vfs_now();
	if (done == 0 && n > 0) return -ENOSPC;
	return (int)done;
}

static int r_symlink(Vnode *dir, const char *name, const char *target) {
	RNode *n;
	Vnode *v;
	int e = make(dir, name, VT_LNK, 0777, &n);
	if (e) return e;
	v = vnode_of(dir->fs, n);
	if (!v) return -ENOMEM;
	e = r_write(v, target, 0, strlen(target), NULL);
	vput(v);
	return e < 0 ? e : 0;
}

static int r_readlink(Vnode *v, char *buf, unsigned n) {
	return r_read(v, buf, 0, n, NULL);
}

static int r_readdir(Vnode *v, unsigned *off, Dirent *de) {
	RNode *d = N(v);
	REnt *e = d->ents;
	unsigned i;
	if (d->type != VT_DIR) return -ENOTDIR;
	if (*off == 0) {
		de->inode = d->ino;
		strncpy(de->name, ".", 28);
		*off = 1;
		return 1;
	}
	if (*off == 1) {
		de->inode = d->parent ? d->parent->ino : d->ino;
		strncpy(de->name, "..", 28);
		*off = 2;
		return 1;
	}
	for (i = 2; e && i < *off; i++) e = e->next;
	if (!e) return 0;
	de->inode = e->node->ino;
	strncpy(de->name, e->name, 28);
	(*off)++;
	return 1;
}

static int r_getattr(Vnode *v, Stat2 *st) {
	RNode *n = N(v);
	static const unsigned tb[7] = {0, S_IFREG, S_IFDIR, S_IFLNK, S_IFCHR, S_IFIFO, S_IFSOCK};
	st->dev = v->fs->dev;
	st->ino = n->ino;
	st->mode = tb[n->type] | n->mode;
	st->nlink = n->nlink;
	st->uid = n->uid;
	st->gid = n->gid;
	st->size = n->size;
	st->atime = n->atime;
	st->mtime = n->mtime;
	st->ctime = n->ctime;
	st->blocks = (n->size + 511) / 512;
	return 0;
}

static int r_setattr(Vnode *v, int what, unsigned a, unsigned b) {
	RNode *n = N(v);
	if (what == SA_MODE) n->mode = a & 07777;
	else if (what == SA_OWNER) {
		if (a != 0xFFFFFFFFu) n->uid = a;
		if (b != 0xFFFFFFFFu) n->gid = b;
	} else if (what == SA_TIMES) {
		n->atime = a;
		n->mtime = b;
	}
	n->ctime = vfs_now();
	return 0;
}

static int r_truncate(Vnode *v, unsigned len) {
	RNode *n = N(v);
	unsigned keep = (len + PAGE - 1) / PAGE;
	if (len < n->size) {
		free_data(R(v), n, keep);
		if (len % PAGE && keep > 0 && keep - 1 < n->npages && n->pages[keep - 1])
			memset((char *)kmap(3, n->pages[keep - 1]) + len % PAGE, 0, PAGE - len % PAGE);
	}
	n->size = len;
	n->mtime = n->ctime = vfs_now();
	return 0;
}

static void r_release(Vnode *v) {
	RNode *n = N(v);
	if (v->nlink_zero && n) {
		free_data(R(v), n, 0);
		kfree(n->pages);
		kfree(n);
	}
}

FsOps ramfs_ops = {
	r_lookup, r_create, r_mkdir, r_unlink, r_rmdir, r_rename, r_link, r_symlink, r_readlink,
	r_read, r_write, r_readdir, r_getattr, r_setattr, r_truncate, NULL, NULL, r_release, r_mknod
};

static unsigned ramdev;

int ramfs_mount(Fs *fs) {
	Ram *r = kmalloc(sizeof(Ram));
	if (!r) return -ENOMEM;
	r->next_ino = 1;
	r->limit = frames_total() / 4;
	r->root = new_node(r, VT_DIR, 01777);
	if (!r->root) return -ENOMEM;
	fs->priv = r;
	fs->ops = &ramfs_ops;
	fs->dev = (2u << 8) | ramdev++;
	fs->root = vnode_of(fs, r->root);
	return fs->root ? 0 : -ENOMEM;
}

static void free_tree(Ram *r, RNode *n) {
	REnt *e = n->ents;
	while (e) {
		REnt *nx = e->next;
		if (e->node->type == VT_DIR) free_tree(r, e->node);
		else if (--e->node->nlink == 0) {
			free_data(r, e->node, 0);
			kfree(e->node->pages);
			kfree(e->node);
		}
		kfree(e);
		e = nx;
	}
	kfree(n);
}

void ramfs_umount(Fs *fs) {
	Ram *r = fs->priv;
	free_tree(r, r->root);
	kfree(r);
}
