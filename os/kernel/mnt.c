/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"
#include "../../lib/include/9p.h"

#define MSIZE 8192
#define MAXDIRENT 128

typedef struct Chan9 {
	File *f;
	unsigned msize, tag, nextfid;
	int dead, busy, ref;
	WaitQueue wq;
	unsigned char *tb, *rb;
	struct Chan9 *next;
} Chan9;

typedef struct Mnt9 {
	Chan9 *c;
	Fs *fs;
	int ref;
	char aname[64];
	char spec[64];
} Mnt9;

typedef struct DirEnt9 {
	unsigned ino;
	char name[28];
} DirEnt9;

typedef struct MntV {
	unsigned fid;
	DirEnt9 *dir;
	int ndir;
} MntV;

static unsigned mnt_dev;
static Chan9 *chans;
extern FsOps mnt_ops;

static const struct {
	const char *s;
	int e;
} errs[] = {
	{"file does not exist", ENOENT},
	{"permission denied", EACCES},
	{"file already exists", EEXIST},
	{"not a directory", ENOTDIR},
	{"is a directory", EISDIR},
	{"directory not empty", ENOTEMPTY},
	{"file in use", EBUSY},
	{"read only file system", EROFS},
	{"name too long", ENAMETOOLONG},
	{"no space left", ENOSPC},
	{"bad offset", EINVAL},
	{"invalid argument", EINVAL},
	{"operation not permitted", EPERM},
	{"bad file descriptor", EBADF},
};

static int err_of(const char *s) {
	unsigned i;
	for (i = 0; i < sizeof(errs) / sizeof(errs[0]); i++)
		if (!strcmp(errs[i].s, s)) return set_errstr(errs[i].e, s, NULL);
	return set_errstr(EIO, s, NULL);
}

static Chan9 *chan_of(Vnode *v) {
	return ((Mnt9 *)v->fs->priv)->c;
}

static void chan_put(Chan9 *c) {
	Chan9 **pp;
	if (--c->ref > 0) return;
	for (pp = &chans; *pp; pp = &(*pp)->next)
		if (*pp == c) {
			*pp = c->next;
			break;
		}
	file_close(c->f);
	kfree(c->tb);
	kfree(c->rb);
	kfree(c);
}

static unsigned fid_of(Vnode *v) {
	return ((MntV *)v->priv)->fid;
}

static int put_all(Chan9 *c, unsigned char *b, unsigned n) {
	unsigned done = 0;
	curproc->nopipe = 1;
	while (done < n) {
		int r = do_write(c->f, (char *)b + done, n - done);
		if (r <= 0) {
			curproc->nopipe = 0;
			return -1;
		}
		done += (unsigned)r;
	}
	curproc->nopipe = 0;
	return 0;
}

static int get_all(Chan9 *c, unsigned char *b, unsigned n, int first) {
	unsigned done = 0;
	while (done < n) {
		int r;
		if (done || !first) curproc->nointr = 1;
		r = do_read(c->f, (char *)b + done, n - done);
		curproc->nointr = 0;
		if (r == -EINTR && done == 0 && first) return -EINTR;
		if (r <= 0) return -1;
		done += (unsigned)r;
	}
	return 0;
}

static void chan_done(Chan9 *c) {
	c->busy = 0;
	wakeup(&c->wq);
}

static void flush(Chan9 *c, unsigned tag) {
	Fcall t;
	unsigned n;
	memset(&t, 0, sizeof(t));
	t.type = Tflush;
	t.tag = (unsigned short)(c->tag++ % NOTAG);
	t.oldtag = (unsigned short)tag;
	n = convS2M(&t, c->tb, c->msize);
	curproc->nointr = 1;
	if (n == 0 || put_all(c, c->tb, n)) c->dead = 1;
	curproc->nointr = 0;
}

static int rpc(Chan9 *c, Fcall *t, Fcall *r) {
	unsigned n, size;
	int e;
	while (c->busy) {
		sleep_on(&c->wq);
		if (signal_pending()) return -EINTR;
	}
	if (c->dead) return set_errstr(EIO, "mount driver: server hung up", NULL);
	c->busy = 1;
	if (t->type != Tversion) t->tag = (unsigned short)(c->tag++ % NOTAG);
	n = convS2M(t, c->tb, c->msize);
	if (n == 0) {
		chan_done(c);
		return -EINVAL;
	}
	curproc->nointr = 1;
	e = put_all(c, c->tb, n);
	curproc->nointr = 0;
	if (e) {
		c->dead = 1;
		chan_done(c);
		return set_errstr(EIO, "mount driver: server hung up", NULL);
	}
	for (;;) {
		e = get_all(c, c->rb, 4, 1);
		if (e == -EINTR) {
			flush(c, t->tag);
			chan_done(c);
			return -EINTR;
		}
		size = e ? 0 : (unsigned)c->rb[0] | ((unsigned)c->rb[1] << 8) | ((unsigned)c->rb[2] << 16) | ((unsigned)c->rb[3] << 24);
		if (e || size < 7 || size > c->msize || get_all(c, c->rb + 4, size - 4, 0) || convM2S(c->rb, size, r) != size) {
			c->dead = 1;
			chan_done(c);
			return set_errstr(EIO, "mount driver: server hung up", NULL);
		}
		if (r->tag == t->tag) break;
	}
	if (r->type == Rerror) {
		e = err_of(r->ename);
		chan_done(c);
		return e;
	}
	if (r->type != t->type + 1) {
		chan_done(c);
		return set_errstr(EIO, "mount driver: unexpected reply", NULL);
	}
	return 0;
}

static int simple(Chan9 *c, Fcall *t) {
	Fcall r;
	int e = rpc(c, t, &r);
	if (e == 0) chan_done(c);
	return e;
}

static int clunk(Chan9 *c, unsigned fid) {
	Fcall t;
	memset(&t, 0, sizeof(t));
	t.type = Tclunk;
	t.fid = fid;
	return simple(c, &t);
}

static int mwalk(Chan9 *c, unsigned fid, const char *name, unsigned *newfid, Qid *q) {
	Fcall t, r;
	int e;
	memset(&t, 0, sizeof(t));
	t.type = Twalk;
	t.fid = fid;
	t.newfid = c->nextfid++;
	t.nwname = name ? 1 : 0;
	t.wname[0] = (char *)name;
	e = rpc(c, &t, &r);
	if (e) return e;
	if (r.nwqid != t.nwname) {
		chan_done(c);
		return set_errstr(ENOENT, "file does not exist", NULL);
	}
	if (q && name) *q = r.wqid[0];
	chan_done(c);
	*newfid = t.newfid;
	return 0;
}

static int open_fid(Chan9 *c, unsigned fid, int mode) {
	Fcall t;
	memset(&t, 0, sizeof(t));
	t.type = Topen;
	t.fid = fid;
	t.mode = (unsigned char)mode;
	return simple(c, &t);
}

static int vtype(Qid *q) {
	return (q->type & QTDIR) ? VT_DIR : (q->type & QTSYMLINK) ? VT_LNK : VT_REG;
}

static Vnode *mvnode(Fs *fs, unsigned fid, Qid *q) {
	Mnt9 *m = fs->priv;
	Chan9 *c = m->c;
	Vnode *v = vget(fs, q->path, vtype(q));
	if (!v) {
		clunk(c, fid);
		return NULL;
	}
	if (v->priv) {
		clunk(c, fid);
		return v;
	}
	v->priv = kmalloc(sizeof(MntV));
	if (!v->priv) {
		clunk(c, fid);
		vput(v);
		return NULL;
	}
	memset(v->priv, 0, sizeof(MntV));
	((MntV *)v->priv)->fid = fid;
	m->ref++;
	return v;
}

static int m_lookup(Vnode *dir, const char *name, Vnode **out) {
	Chan9 *c = chan_of(dir);
	unsigned nf;
	Qid q;
	int e;
	if (!strcmp(name, ".")) {
		*out = vref(dir);
		return 0;
	}
	e = mwalk(c, fid_of(dir), name, &nf, &q);
	if (e) return e;
	*out = mvnode(dir->fs, nf, &q);
	return *out ? 0 : -ENOMEM;
}

static int io(Vnode *v, int type, char *buf, unsigned off, unsigned n, File *f) {
	Chan9 *c = chan_of(v);
	unsigned fid, done = 0, max = c->msize - IOHDRSZ;
	int e = 0, temp = !(f && f->mopen);
	if (temp) {
		e = mwalk(c, fid_of(v), NULL, &fid, NULL);
		if (e) return e;
		e = open_fid(c, fid, type == Tread ? OREAD : OWRITE);
		if (e) {
			clunk(c, fid);
			return e;
		}
	} else fid = f->mfid;
	while (done < n) {
		Fcall t, r;
		unsigned k = n - done > max ? max : n - done;
		memset(&t, 0, sizeof(t));
		t.type = (unsigned char)type;
		t.fid = fid;
		t.offset = off + done;
		t.count = k;
		t.data = buf + done;
		e = rpc(c, &t, &r);
		if (e) break;
		if (type == Tread) {
			if (r.count > k) r.count = k;
			memcpy(buf + done, r.data, r.count);
		}
		chan_done(c);
		done += r.count;
		if (r.count < k || type == Tread) break;
	}
	if (temp) clunk(c, fid);
	return done ? (int)done : e;
}

static int m_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	return io(v, Tread, buf, off, n, f);
}

static int m_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	return io(v, Twrite, (char *)buf, off, n, f);
}

static int m_readlink(Vnode *v, char *buf, unsigned n) {
	return io(v, Tread, buf, 0, n, NULL);
}

static int load_dir(Vnode *v) {
	Chan9 *c = chan_of(v);
	MntV *m = v->priv;
	unsigned fid, off = 0;
	char *b;
	int e;
	if (!m->dir) m->dir = kmalloc(sizeof(DirEnt9) * MAXDIRENT);
	if (!m->dir) return -ENOMEM;
	m->ndir = 0;
	b = kmalloc(c->msize);
	if (!b) return -ENOMEM;
	e = mwalk(c, fid_of(v), NULL, &fid, NULL);
	if (e == 0) {
		e = open_fid(c, fid, OREAD);
		while (e == 0) {
			Fcall t, r;
			unsigned i = 0, got;
			memset(&t, 0, sizeof(t));
			t.type = Tread;
			t.fid = fid;
			t.offset = off;
			t.count = c->msize - IOHDRSZ;
			e = rpc(c, &t, &r);
			if (e) break;
			got = r.count;
			memcpy(b, r.data, got);
			chan_done(c);
			if (got == 0) break;
			off += got;
			while (i + 2 <= got) {
				Stat9 d;
				unsigned k = convM2D((unsigned char *)b + i, got - i, &d);
				if (k == 0) break;
				if (m->ndir < MAXDIRENT) {
					m->dir[m->ndir].ino = d.qid.path;
					strncpy(m->dir[m->ndir].name, d.name, 27);
					m->dir[m->ndir].name[27] = 0;
					m->ndir++;
				}
				i += k;
			}
		}
		clunk(c, fid);
	}
	kfree(b);
	return e;
}

static int m_readdir(Vnode *v, unsigned *off, Dirent *de) {
	MntV *m = v->priv;
	if (*off == 0) {
		int e = load_dir(v);
		if (e) return e;
	}
	if (*off < 2) {
		de->inode = v->ino;
		strncpy(de->name, *off ? ".." : ".", 28);
		(*off)++;
		return 1;
	}
	if (!m->dir || *off - 2 >= (unsigned)m->ndir) return 0;
	de->inode = m->dir[*off - 2].ino;
	strncpy(de->name, m->dir[*off - 2].name, 28);
	(*off)++;
	return 1;
}

static unsigned num(const char *s) {
	unsigned v = 0;
	for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (unsigned)(*s - '0');
	return v;
}

static int m_getattr(Vnode *v, Stat2 *st) {
	Chan9 *c = chan_of(v);
	Fcall t, r;
	Stat9 d;
	int e;
	memset(&t, 0, sizeof(t));
	t.type = Tstat;
	t.fid = fid_of(v);
	e = rpc(c, &t, &r);
	if (e) return e;
	if (convM2D(r.stat, r.nstat, &d) == 0) {
		chan_done(c);
		return -EIO;
	}
	st->dev = v->fs->dev;
	st->ino = d.qid.path;
	st->mode = ((d.mode & DMDIR) ? S_IFDIR : (d.mode & DMSYMLINK) ? S_IFLNK : S_IFREG) | (d.mode & 0777);
	st->nlink = (d.mode & DMDIR) ? 2 : 1;
	st->uid = num(d.uid);
	st->gid = num(d.gid);
	st->size = d.length;
	st->atime = d.atime;
	st->mtime = st->ctime = d.mtime;
	st->blocks = (d.length + 511) / 512;
	chan_done(c);
	return 0;
}

static int wstat(Vnode *v, Stat9 *d) {
	Chan9 *c = chan_of(v);
	unsigned char b[256];
	Fcall t;
	memset(&t, 0, sizeof(t));
	t.type = Twstat;
	t.fid = fid_of(v);
	t.nstat = convD2M(d, b, sizeof(b));
	t.stat = b;
	if (t.nstat == 0) return -ENAMETOOLONG;
	return simple(c, &t);
}

static int m_setattr(Vnode *v, int what, unsigned a, unsigned b) {
	Stat9 d;
	Stat2 st;
	char u[12], g[12];
	nulldir9(&d);
	if (what == SA_MODE) {
		int e = m_getattr(v, &st);
		if (e) return e;
		d.mode = (v->type == VT_DIR ? DMDIR : v->type == VT_LNK ? DMSYMLINK : 0) | (a & 0777);
	} else if (what == SA_OWNER) {
		if (a != 0xFFFFFFFFu) {
			ksnprintf(u, sizeof(u), "%u", a);
			d.uid = u;
		}
		if (b != 0xFFFFFFFFu) {
			ksnprintf(g, sizeof(g), "%u", b);
			d.gid = g;
		}
	} else if (what == SA_TIMES) {
		d.atime = a;
		d.mtime = b;
	}
	return wstat(v, &d);
}

static int m_truncate(Vnode *v, unsigned len) {
	Stat9 d;
	if (v->type == VT_DIR) return -EISDIR;
	nulldir9(&d);
	d.length = len;
	d.lengthhi = 0;
	return wstat(v, &d);
}

static int mcreate(Vnode *dir, const char *name, unsigned perm, int mode, const char *data, Vnode **out) {
	Chan9 *c = chan_of(dir);
	unsigned fid;
	Fcall t, r;
	int e = mwalk(c, fid_of(dir), NULL, &fid, NULL);
	if (e) return e;
	memset(&t, 0, sizeof(t));
	t.type = Tcreate;
	t.fid = fid;
	t.name = (char *)name;
	t.perm = perm;
	t.mode = (unsigned char)mode;
	e = rpc(c, &t, &r);
	if (e) {
		clunk(c, fid);
		return e;
	}
	chan_done(c);
	if (data) {
		memset(&t, 0, sizeof(t));
		t.type = Twrite;
		t.fid = fid;
		t.count = strlen(data);
		t.data = (char *)data;
		e = simple(c, &t);
	}
	clunk(c, fid);
	if (e == 0 && out) e = m_lookup(dir, name, out);
	return e;
}

static int m_create(Vnode *dir, const char *name, unsigned mode, Vnode **out) {
	return mcreate(dir, name, mode & 0777, OREAD, NULL, out);
}

static int m_mkdir(Vnode *dir, const char *name, unsigned mode) {
	return mcreate(dir, name, DMDIR | (mode & 0777), OREAD, NULL, NULL);
}

static int m_symlink(Vnode *dir, const char *name, const char *target) {
	return mcreate(dir, name, DMSYMLINK | 0777, OWRITE, target, NULL);
}

static int m_remove(Vnode *dir, const char *name) {
	Chan9 *c = chan_of(dir);
	unsigned fid;
	Fcall t;
	int e = mwalk(c, fid_of(dir), name, &fid, NULL);
	if (e) return e;
	memset(&t, 0, sizeof(t));
	t.type = Tremove;
	t.fid = fid;
	return simple(c, &t);
}

static int m_rename(Vnode *odir, const char *oname, Vnode *ndir, const char *nname) {
	Vnode *v, *t;
	Stat9 d;
	int e;
	if (odir != ndir) return -EXDEV;
	e = m_lookup(odir, oname, &v);
	if (e) return e;
	if (m_lookup(ndir, nname, &t) == 0) {
		int dir = t->type == VT_DIR;
		vput(t);
		if (dir != (v->type == VT_DIR)) {
			vput(v);
			return dir ? -EISDIR : -ENOTDIR;
		}
		e = m_remove(ndir, nname);
		if (e) {
			vput(v);
			return e;
		}
	}
	nulldir9(&d);
	d.name = (char *)nname;
	e = wstat(v, &d);
	vput(v);
	return e;
}

static int m_poll(Vnode *v, File *f) {
	(void)v;
	(void)f;
	return POLLIN | POLLOUT;
}

static void m_release(Vnode *v) {
	Mnt9 *mt = v->fs->priv;
	Chan9 *c = mt->c;
	MntV *m = v->priv;
	if (!m) return;
	if (!c->dead) clunk(c, m->fid);
	kfree(m->dir);
	kfree(m);
	v->priv = NULL;
	if (--mt->ref == 0) {
		kfree(mt->fs);
		kfree(mt);
		chan_put(c);
	}
}

FsOps mnt_ops = {
	m_lookup, m_create, m_mkdir, m_remove, m_remove, m_rename, NULL, m_symlink, m_readlink,
	m_read, m_write, m_readdir, m_getattr, m_setattr, m_truncate, NULL, m_poll, m_release, NULL
};

int mnt_open(Vnode *v, File *f, int flags) {
	Chan9 *c;
	unsigned fid;
	int e, mode;
	if (v->fs->ops != &mnt_ops) return 0;
	c = chan_of(v);
	mode = (flags & 3) == O_WRONLY ? OWRITE : (flags & 3) == O_RDWR ? ORDWR : OREAD;
	e = mwalk(c, fid_of(v), NULL, &fid, NULL);
	if (e) return e;
	e = open_fid(c, fid, mode);
	if (e) {
		clunk(c, fid);
		return e;
	}
	f->mfid = fid;
	f->mopen = 1;
	return 0;
}

void mnt_close(File *f) {
	if (f->mopen && f->vn && f->vn->fs->ops == &mnt_ops && !chan_of(f->vn)->dead) clunk(chan_of(f->vn), f->mfid);
	f->mopen = 0;
}

const char *mnt_spec(Fs *fs, const char **aname) {
	Mnt9 *m = fs->priv;
	*aname = m->aname;
	return m->spec;
}

int is_mnt(Fs *fs) {
	return fs->ops == &mnt_ops;
}

static Chan9 *session(File *f, int *err) {
	Chan9 *c;
	Fcall t, r;
	int e;
	for (c = chans; c; c = c->next)
		if (c->f == f && !c->dead) {
			c->ref++;
			return c;
		}
	c = kmalloc(sizeof(Chan9));
	if (!c) {
		*err = -ENOMEM;
		return NULL;
	}
	memset(c, 0, sizeof(Chan9));
	c->tb = kmalloc(MSIZE);
	c->rb = kmalloc(MSIZE);
	if (!c->tb || !c->rb) {
		kfree(c->tb);
		kfree(c->rb);
		kfree(c);
		*err = -ENOMEM;
		return NULL;
	}
	c->f = file_dup(f);
	c->msize = MSIZE;
	c->nextfid = 1;
	c->ref = 1;
	c->next = chans;
	chans = c;
	memset(&t, 0, sizeof(t));
	t.type = Tversion;
	t.tag = NOTAG;
	t.msize = MSIZE;
	t.version = VERSION9P;
	e = rpc(c, &t, &r);
	if (e == 0) {
		if (r.msize < 256 || strcmp(r.version, VERSION9P)) e = set_errstr(EIO, "mount driver: bad version", NULL);
		else if (r.msize < c->msize) c->msize = r.msize;
		chan_done(c);
	}
	if (e) {
		chan_put(c);
		*err = e;
		return NULL;
	}
	return c;
}

int sys_mount9(int fd, int afd, unsigned uold, int flags, unsigned uaname) {
	File *f = getf(fd);
	Chan9 *c;
	Mnt9 *m;
	Fs *fs;
	Fcall t, r;
	Walk *w;
	Vnode *root;
	char *old;
	char uname[12];
	const char *srv;
	unsigned fid;
	int e = 0;
	if (!f) return -EBADF;
	if (afd != -1) return set_errstr(EINVAL, "authentication is not supported", NULL);
	if ((flags & ~7) || (flags & 3) == 3) return -EINVAL;
	if (ns_current() == ns_boot() && curproc->euid) return -EPERM;
	m = kmalloc(sizeof(Mnt9));
	fs = kmalloc(sizeof(Fs));
	old = kmalloc(MAXPATH);
	w = kmalloc(sizeof(Walk));
	if (!m || !fs || !old || !w) {
		e = -ENOMEM;
		goto fail;
	}
	memset(m, 0, sizeof(Mnt9));
	memset(fs, 0, sizeof(Fs));
	e = copyinstr(old, uold, MAXPATH);
	if (e >= 0 && uaname) e = copyinstr(m->aname, uaname, sizeof(m->aname));
	if (e < 0) goto fail;
	e = 0;
	c = session(f, &e);
	if (!c) goto fail;
	m->c = c;
	m->fs = fs;
	srv = srv_name_of(f);
	if (srv) ksnprintf(m->spec, sizeof(m->spec), "#s/%s", srv);
	else fd_name(curproc, fd, m->spec, sizeof(m->spec));
	fs->ops = &mnt_ops;
	fs->priv = m;
	fs->dev = (41u << 8) | (mnt_dev++ & 0xFF);
	strncpy(fs->source, m->spec, 63);
	strncpy(fs->fstype, "9p", 7);
	ksnprintf(uname, sizeof(uname), "%u", curproc->euid);
	fid = c->nextfid++;
	memset(&t, 0, sizeof(t));
	t.type = Tattach;
	t.fid = fid;
	t.afid = NOFID;
	t.uname = uname;
	t.aname = m->aname;
	e = rpc(c, &t, &r);
	if (e) {
		chan_put(c);
		goto fail;
	}
	{
		Qid q = r.qid;
		chan_done(c);
		root = mvnode(fs, fid, &q);
	}
	if (!root) {
		chan_put(c);
		e = -ENOMEM;
		goto fail;
	}
	fs->root = root;
	e = vfs_walk(old, WFOLLOW | WNOMNT, w);
	if (e == 0) {
		if ((w->key->type == VT_DIR) != (root->type == VT_DIR)) e = -ENOTDIR;
		else e = ns_add(ns_current(), w->key, w->path, root, m->spec, flags | M9P);
		walk_done(w);
	}
	vput(root);
	kfree(old);
	kfree(w);
	return e;
fail:
	kfree(m);
	kfree(fs);
	kfree(old);
	kfree(w);
	return e;
}
