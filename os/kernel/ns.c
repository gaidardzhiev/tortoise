/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define NMNT 32
#define MAXSTEP 64

typedef struct Mnt {
	Vnode *key;
	char *oldname;
	int n;
	Vnode *mem[NMEMB];
	char *names[NMEMB];
	int flags[NMEMB];
} Mnt;

typedef struct Ns {
	int ref;
	int n;
	Mnt m[NMNT];
	struct Ns *next;
} Ns;

typedef struct WalkState {
	Vnode *keys[MAXSTEP];
	Vnode *vs[MAXSTEP];
	unsigned plen[MAXSTEP];
	char path[MAXPATH];
	char buf[MAXPATH];
	char rest[MAXPATH];
} WalkState;

static Ns bootns;
static Ns *allns = &bootns;
static int booted;

static char *sdup(const char *s) {
	unsigned l = strlen(s) + 1;
	char *d = kmalloc(l);
	if (d) memcpy(d, s, l);
	return d;
}

Ns *ns_boot(void) {
	if (!booted) {
		bootns.ref = 1;
		booted = 1;
	}
	return &bootns;
}

Ns *ns_current(void) {
	return curproc && curproc->ns ? curproc->ns : ns_boot();
}

Ns *ns_hold(Ns *ns) {
	ns->ref++;
	return ns;
}

static Mnt *find(Ns *ns, Vnode *key) {
	int i;
	for (i = 0; i < ns->n; i++)
		if (ns->m[i].key == key) return &ns->m[i];
	return NULL;
}

static void clear_entry(Mnt *e) {
	int i;
	for (i = 0; i < e->n; i++) {
		vput(e->mem[i]);
		kfree(e->names[i]);
	}
	vput(e->key);
	kfree(e->oldname);
	memset(e, 0, sizeof(Mnt));
}

static void remove_entry(Ns *ns, Mnt *e) {
	int i = (int)(e - ns->m);
	clear_entry(e);
	for (; i + 1 < ns->n; i++) ns->m[i] = ns->m[i + 1];
	ns->n--;
	memset(&ns->m[ns->n], 0, sizeof(Mnt));
}

static Ns *ns_alloc(void) {
	Ns *ns = kmalloc(sizeof(Ns));
	if (!ns) return NULL;
	memset(ns, 0, sizeof(Ns));
	ns->ref = 1;
	ns->next = allns;
	allns = ns;
	return ns;
}

Ns *ns_clean(void) {
	return ns_alloc();
}

Ns *ns_copy(Ns *src) {
	Ns *ns = ns_alloc();
	int i, k;
	if (!ns) return NULL;
	for (i = 0; i < src->n; i++) {
		Mnt *s = &src->m[i], *d = &ns->m[i];
		d->key = vref(s->key);
		d->oldname = sdup(s->oldname ? s->oldname : "?");
		d->n = s->n;
		for (k = 0; k < s->n; k++) {
			d->mem[k] = vref(s->mem[k]);
			d->names[k] = sdup(s->names[k] ? s->names[k] : "?");
			d->flags[k] = s->flags[k];
		}
	}
	ns->n = src->n;
	return ns;
}

void ns_put(Ns *ns) {
	Ns **pp;
	if (!ns || --ns->ref > 0 || ns == &bootns) return;
	while (ns->n > 0) remove_entry(ns, &ns->m[ns->n - 1]);
	for (pp = &allns; *pp; pp = &(*pp)->next)
		if (*pp == ns) {
			*pp = ns->next;
			break;
		}
	kfree(ns);
}

int ns_add(Ns *ns, Vnode *key, const char *oldname, Vnode *mem, const char *newname, int flags) {
	Mnt *e = find(ns, key);
	int i, how = flags & 3;
	if (e && how == MREPLACE) {
		for (i = 0; i < e->n; i++) {
			vput(e->mem[i]);
			kfree(e->names[i]);
		}
		e->n = 0;
	}
	if (!e) {
		if (ns->n >= NMNT) return -ENOSPC;
		e = &ns->m[ns->n++];
		memset(e, 0, sizeof(Mnt));
		e->key = vref(key);
		e->oldname = sdup(oldname);
		if (how != MREPLACE) {
			e->mem[0] = vref(key);
			e->names[0] = sdup(oldname);
			e->flags[0] = 0;
			e->n = 1;
		}
	}
	if (e->n >= NMEMB) return -ENOSPC;
	if (how == MBEFORE) {
		for (i = e->n; i > 0; i--) {
			e->mem[i] = e->mem[i - 1];
			e->names[i] = e->names[i - 1];
			e->flags[i] = e->flags[i - 1];
		}
		i = 0;
	} else {
		i = e->n;
	}
	e->mem[i] = vref(mem);
	e->names[i] = sdup(newname);
	e->flags[i] = flags & ~3;
	e->n++;
	return 0;
}

int ns_is_mountpoint(Vnode *v) {
	return find(ns_current(), v) != NULL;
}

int ns_union(Vnode *key, Vnode **mem, int max) {
	Mnt *e = find(ns_current(), key);
	int i;
	if (!e || e->n < 2) return 0;
	for (i = 0; i < e->n && i < max; i++) mem[i] = vref(e->mem[i]);
	return i;
}

int ns_fs_refs(Fs *fs, Vnode *v) {
	Ns *ns;
	int i, k, n = 0;
	for (ns = allns; ns; ns = ns->next)
		for (i = 0; i < ns->n; i++) {
			if (ns->m[i].key == v && v->fs == fs) n++;
			for (k = 0; k < ns->m[i].n; k++)
				if (ns->m[i].mem[k] == v && v->fs == fs) n++;
		}
	return n;
}

void ns_drop_fs(Fs *fs) {
	Ns *ns;
	int i, k;
	for (ns = allns; ns; ns = ns->next)
		for (i = ns->n - 1; i >= 0; i--) {
			Mnt *e = &ns->m[i];
			if (e->key->fs == fs) {
				remove_entry(ns, e);
				continue;
			}
			for (k = e->n - 1; k >= 0; k--)
				if (e->mem[k]->fs == fs) {
					int j;
					vput(e->mem[k]);
					kfree(e->names[k]);
					for (j = k; j + 1 < e->n; j++) {
						e->mem[j] = e->mem[j + 1];
						e->names[j] = e->names[j + 1];
						e->flags[j] = e->flags[j + 1];
					}
					e->n--;
				}
			if (e->n == 0) remove_entry(ns, e);
		}
}

int ns_remove_fs_member(Ns *ns, Vnode *key, Vnode *root) {
	Mnt *e = find(ns, key);
	int k;
	if (!e) return -EINVAL;
	for (k = 0; k < e->n; k++)
		if (e->mem[k] == root) return 0;
	return -EINVAL;
}

static Vnode *apply(Ns *ns, Vnode *k) {
	Mnt *e = find(ns, k);
	return vref(e ? e->mem[0] : k);
}

static int lookup_in(Ns *ns, Vnode *key, Vnode *v, const char *name, Vnode **out) {
	Mnt *e = find(ns, key);
	int i, r = -ENOENT, denied = 0;
	if (e && e->n > 1) {
		for (i = 0; i < e->n; i++) {
			Vnode *m = e->mem[i];
			if (m->type != VT_DIR) continue;
			if (curproc && vfs_access(m, 1)) {
				denied = 1;
				continue;
			}
			if (m->fs->ops->lookup(m, name, out) == 0) return 0;
		}
		return denied ? -EACCES : r;
	}
	if (curproc && (r = vfs_access(v, 1)) != 0) return r;
	return v->fs->ops->lookup(v, name, out);
}

static void parent_path(const char *p, char *out) {
	int l = (int)strlen(p);
	strncpy(out, p, MAXPATH - 1);
	out[MAXPATH - 1] = 0;
	while (l > 1 && out[l - 1] == '/') out[--l] = 0;
	while (l > 0 && out[l - 1] != '/') l--;
	while (l > 1 && out[l - 1] == '/') l--;
	if (l <= 0) l = 1;
	out[l] = 0;
}

static int walk_depth;

static int has_slash(const char *s) {
	for (; *s; s++)
		if (*s == '/') return 1;
	return 0;
}

int vfs_walk(const char *path, int flags, Walk *w) {
	WalkState *st;
	Ns *ns = ns_current();
	char *p, comp[28];
	int d = 0, links = 0, r = 0, i;
	if (!path[0]) return -ENOENT;
	if (strlen(path) >= MAXPATH) return -ENAMETOOLONG;
	if (walk_depth > 2) return -ELOOP;
	st = kmalloc(sizeof(WalkState));
	if (!st) return -ENOMEM;
	walk_depth++;
	strncpy(st->buf, path, MAXPATH);
	p = st->buf;
	if (*p == '#') {
		Vnode *dr = p[1] && (p[2] == 0 || p[2] == '/') ? dev9_root(p[1]) : NULL;
		if (!dr) {
			kfree(st);
			walk_depth--;
			return set_errstr(ENOENT, "unknown device", path);
		}
		st->keys[0] = dr;
		st->vs[0] = apply(ns, dr);
		st->path[0] = '#';
		st->path[1] = p[1];
		st->path[2] = 0;
		p += 2;
	} else if (*p == '/' || !curproc || !curproc->cwd) {
		st->keys[0] = vref(rootvn);
		st->vs[0] = apply(ns, rootvn);
		strncpy(st->path, "/", 2);
	} else {
		st->keys[0] = vref(curproc->cwdkey ? curproc->cwdkey : curproc->cwd);
		st->vs[0] = vref(curproc->cwd);
		strncpy(st->path, curproc->cwdpath[0] ? curproc->cwdpath : "/", MAXPATH);
	}
	st->plen[0] = strlen(st->path);
	for (;;) {
		Vnode *next, *cur = st->vs[d];
		int k = 0, last;
		while (*p == '/') p++;
		if (!*p) {
			if (flags & WPARENT) {
				r = -EINVAL;
				break;
			}
			break;
		}
		while (*p && *p != '/') {
			if (k >= 27) {
				r = -ENAMETOOLONG;
				break;
			}
			comp[k++] = *p++;
		}
		if (r) break;
		comp[k] = 0;
		while (*p == '/') p++;
		last = *p == 0;
		if (cur->type != VT_DIR) {
			r = -ENOTDIR;
			break;
		}
		if (last && (flags & WPARENT)) {
			strncpy(w->name, comp, 28);
			if (!strcmp(comp, ".") || !strcmp(comp, "..")) {
				if (curproc && (r = vfs_access(cur, 1)) != 0) break;
			}
			break;
		}
		if (!strcmp(comp, ".")) {
			if (curproc && (r = vfs_access(cur, 1)) != 0) break;
			continue;
		}
		if (!strcmp(comp, "..")) {
			if (curproc && (r = vfs_access(cur, 1)) != 0) break;
			if (d > 0) {
				vput(st->keys[d]);
				vput(st->vs[d]);
				d--;
				st->path[st->plen[d]] = 0;
				continue;
			}
			if (!strcmp(st->path, "/") || (st->path[0] == '#' && !has_slash(st->path))) continue;
			{
				Walk *up = kmalloc(sizeof(Walk));
				char *pp = kmalloc(MAXPATH);
				if (!up || !pp) {
					kfree(up);
					kfree(pp);
					r = -ENOMEM;
					break;
				}
				parent_path(st->path, pp);
				r = vfs_walk(pp, WFOLLOW, up);
				kfree(pp);
				if (r) {
					kfree(up);
					break;
				}
				vput(st->keys[0]);
				vput(st->vs[0]);
				st->keys[0] = up->key;
				st->vs[0] = up->v;
				strncpy(st->path, up->path, MAXPATH);
				st->plen[0] = strlen(st->path);
				kfree(up);
			}
			continue;
		}
		r = lookup_in(ns, st->keys[d], cur, comp, &next);
		if (r) break;
		if (next->type == VT_LNK && (!last || (flags & WFOLLOW))) {
			char tgt[256];
			int n;
			if (++links > 8) {
				vput(next);
				r = -ELOOP;
				break;
			}
			n = next->fs->ops->readlink(next, tgt, 255);
			vput(next);
			if (n <= 0) {
				r = -ENOENT;
				break;
			}
			tgt[n] = 0;
			if (strlen(tgt) + strlen(p) + 2 >= MAXPATH) {
				r = -ENAMETOOLONG;
				break;
			}
			strncpy(st->rest, p, MAXPATH);
			strncpy(st->buf, tgt, MAXPATH);
			if (st->rest[0]) {
				unsigned l = strlen(st->buf);
				st->buf[l] = '/';
				strncpy(st->buf + l + 1, st->rest, MAXPATH - l - 1);
			}
			p = st->buf;
			if (tgt[0] == '/') {
				while (d > 0) {
					vput(st->keys[d]);
					vput(st->vs[d]);
					d--;
				}
				vput(st->keys[0]);
				vput(st->vs[0]);
				st->keys[0] = vref(rootvn);
				st->vs[0] = apply(ns, rootvn);
				strncpy(st->path, "/", 2);
				st->plen[0] = 1;
			}
			continue;
		}
		if (d + 1 >= MAXSTEP || st->plen[d] + strlen(comp) + 2 >= MAXPATH) {
			vput(next);
			r = -ENAMETOOLONG;
			break;
		}
		d++;
		st->keys[d] = next;
		st->vs[d] = last && (flags & WNOMNT) ? vref(next) : apply(ns, next);
		{
			unsigned l = st->plen[d - 1];
			if (l > 1) st->path[l++] = '/';
			strncpy(st->path + l, comp, MAXPATH - l);
			st->plen[d] = strlen(st->path);
		}
	}
	if (r == 0) {
		w->key = st->keys[d];
		w->v = st->vs[d];
		strncpy(w->path, st->path, MAXPATH);
		for (i = 0; i < d; i++) {
			vput(st->keys[i]);
			vput(st->vs[i]);
		}
	} else {
		for (i = 0; i <= d; i++) {
			vput(st->keys[i]);
			vput(st->vs[i]);
		}
	}
	kfree(st);
	walk_depth--;
	return r;
}

void walk_done(Walk *w) {
	vput(w->key);
	vput(w->v);
}

int vfs_resolve(const char *path, Vnode *cwd, int follow, Vnode **out) {
	Walk *w = kmalloc(sizeof(Walk));
	int r;
	(void)cwd;
	if (!w) return -ENOMEM;
	r = vfs_walk(path, follow ? WFOLLOW : 0, w);
	if (r == 0) {
		*out = w->v;
		vput(w->key);
	}
	kfree(w);
	return r;
}

int vfs_parent(const char *path, Vnode *cwd, Vnode **dir, char *name) {
	Walk *w = kmalloc(sizeof(Walk));
	Mnt *e;
	int r, i;
	(void)cwd;
	if (!w) return -ENOMEM;
	r = vfs_walk(path, WFOLLOW | WPARENT, w);
	if (r) {
		kfree(w);
		return r;
	}
	strncpy(name, w->name, 28);
	e = find(ns_current(), w->key);
	if (e && (e->n > 1 || !(e->flags[0] & (MCREATE | MFS)))) {
		Vnode *t;
		Vnode *pick = NULL;
		for (i = 0; i < e->n && !pick; i++)
			if (e->mem[i]->type == VT_DIR && e->mem[i]->fs->ops->lookup(e->mem[i], name, &t) == 0) {
				vput(t);
				pick = e->mem[i];
			}
		for (i = 0; i < e->n && !pick; i++)
			if (e->mem[i]->type == VT_DIR && (e->flags[i] & (MCREATE | MFS))) pick = e->mem[i];
		if (!pick) {
			walk_done(w);
			kfree(w);
			return -EROFS;
		}
		*dir = vref(pick);
		walk_done(w);
	} else {
		*dir = w->v;
		vput(w->key);
	}
	kfree(w);
	return 0;
}

static int copy_upath(char *k, unsigned u) {
	int r = copyinstr(k, u, MAXPATH);
	if (r < 0) return r == -EINVAL ? -ENAMETOOLONG : r;
	return 0;
}

int sys_bind9(unsigned unew, unsigned uold, int flags) {
	Walk *a, *b;
	char *pn, *po;
	Ns *ns = ns_current();
	int r;
	if ((flags & ~7) || (flags & 3) == 3) return -EINVAL;
	if (ns == &bootns && curproc->euid) return -EPERM;
	a = kmalloc(sizeof(Walk));
	b = kmalloc(sizeof(Walk));
	pn = kmalloc(MAXPATH);
	po = kmalloc(MAXPATH);
	r = (!a || !b || !pn || !po) ? -ENOMEM : copy_upath(pn, unew);
	if (r == 0) r = copy_upath(po, uold);
	if (r == 0) r = vfs_walk(pn, WFOLLOW, a);
	if (r == 0) {
		r = vfs_walk(po, WFOLLOW | WNOMNT, b);
		if (r == 0) {
			if ((b->key->type == VT_DIR) != (a->v->type == VT_DIR)) r = b->key->type == VT_DIR ? -ENOTDIR : -EISDIR;
			else r = ns_add(ns, b->key, b->path, a->v, a->path, flags);
			walk_done(b);
		}
		walk_done(a);
	}
	kfree(a);
	kfree(b);
	kfree(pn);
	kfree(po);
	return r;
}

int sys_unmount(unsigned unew, unsigned uold) {
	Walk *a = kmalloc(sizeof(Walk)), *b = kmalloc(sizeof(Walk));
	char *p = kmalloc(MAXPATH);
	Ns *ns = ns_current();
	Mnt *e;
	int r, k;
	if (!a || !b || !p) {
		kfree(a);
		kfree(b);
		kfree(p);
		return -ENOMEM;
	}
	if (ns == &bootns && curproc->euid) r = -EPERM;
	else r = copy_upath(p, uold);
	if (r == 0) r = vfs_walk(p, WFOLLOW | WNOMNT, b);
	if (r == 0) {
		e = find(ns, b->key);
		if (!e) r = -EINVAL;
		else if (!unew) remove_entry(ns, e);
		else {
			r = copy_upath(p, unew);
			if (r == 0) r = vfs_walk(p, WFOLLOW, a);
			if (r == 0) {
				int found = -1;
				for (k = 0; k < e->n; k++)
					if (e->mem[k] == a->v || (e->mem[k] == a->key && e->mem[k] == b->key)) found = k;
				if (found < 0) r = -EINVAL;
				else {
					vput(e->mem[found]);
					kfree(e->names[found]);
					for (k = found; k + 1 < e->n; k++) {
						e->mem[k] = e->mem[k + 1];
						e->names[k] = e->names[k + 1];
						e->flags[k] = e->flags[k + 1];
					}
					e->n--;
					if (e->n == 0) remove_entry(ns, e);
				}
				walk_done(a);
			}
		}
		walk_done(b);
	}
	kfree(a);
	kfree(b);
	kfree(p);
	return r;
}

static unsigned put_s(char *b, unsigned at, unsigned max, const char *s) {
	unsigned l = strlen(s);
	if (at + l >= max) l = max - at - 1;
	memcpy(b + at, s, l);
	return at + l;
}

static unsigned put_name(char *b, unsigned at, unsigned max, const char *s) {
	if (s[0] != '#') return put_s(b, at, max, s);
	at = put_s(b, at, max, "'");
	at = put_s(b, at, max, s);
	return put_s(b, at, max, "'");
}

static unsigned member_line(char *b, unsigned at, unsigned max, Mnt *e, int k, int how) {
	Vnode *m = e->mem[k];
	char flag[5];
	int c = !(e->flags[k] & MFS) && (e->flags[k] & MCREATE), l = 0;
	if (how || c) {
		flag[l++] = '-';
		if (how) flag[l++] = (char)how;
		if (c) flag[l++] = 'c';
		flag[l++] = ' ';
	}
	flag[l] = 0;
	if (e->flags[k] & M9P) {
		const char *an, *spec = mnt_spec(m->fs, &an);
		at = put_s(b, at, max, "mount ");
		at = put_s(b, at, max, flag);
		at = put_name(b, at, max, spec);
		at = put_s(b, at, max, " ");
		at = put_name(b, at, max, e->oldname);
		if (an[0]) {
			at = put_s(b, at, max, " ");
			at = put_s(b, at, max, an);
		}
		return put_s(b, at, max, "\n");
	}
	if (e->flags[k] & MFS) {
		at = put_s(b, at, max, "mount ");
		at = put_s(b, at, max, flag);
		at = put_s(b, at, max, "-t ");
		at = put_s(b, at, max, m->fs->fstype);
		at = put_s(b, at, max, " ");
		at = put_s(b, at, max, m->fs->source);
	} else {
		at = put_s(b, at, max, "bind ");
		at = put_s(b, at, max, flag);
		at = put_name(b, at, max, e->names[k]);
	}
	at = put_s(b, at, max, " ");
	at = put_name(b, at, max, e->oldname);
	return put_s(b, at, max, "\n");
}

int ns_text(Ns *ns, char *b, unsigned max) {
	unsigned at = 0;
	int i, k;
	if (!ns) ns = ns_boot();
	for (i = 0; i < ns->n; i++) {
		Mnt *e = &ns->m[i];
		int anchor = -1;
		for (k = 0; k < e->n; k++)
			if (e->mem[k] == e->key && !(e->flags[k] & MFS)) anchor = k;
		if (anchor < 0)
			for (k = 0; k < e->n && anchor < 0; k++)
				if (e->flags[k] & MFS) anchor = k;
		if (anchor < 0) anchor = 0;
		if (e->mem[anchor] != e->key) at = member_line(b, at, max, e, anchor, 0);
		for (k = anchor - 1; k >= 0; k--) at = member_line(b, at, max, e, k, 'b');
		for (k = anchor + 1; k < e->n; k++) at = member_line(b, at, max, e, k, 'a');
	}
	b[at] = 0;
	return (int)at;
}

int sys_fd2path(int fd, unsigned ubuf, unsigned n) {
	File *f = getf(fd);
	char buf[MAXPATH];
	unsigned l;
	if (!f) return -EBADF;
	fd_name(curproc, fd, buf, sizeof(buf));
	buf[MAXPATH - 1] = 0;
	l = strlen(buf) + 1;
	if (l > n) return -ERANGE;
	return copyout(ubuf, buf, l) ? -EFAULT : 0;
}

void ns_enter_boot(void) {
	Walk *w = kmalloc(sizeof(Walk));
	ns_put(curproc->ns);
	curproc->ns = ns_hold(ns_boot());
	if (w && vfs_walk(curproc->cwdpath[0] ? curproc->cwdpath : "/", WFOLLOW, w) == 0) {
		vput(curproc->cwd);
		if (curproc->cwdkey) vput(curproc->cwdkey);
		curproc->cwd = w->v;
		curproc->cwdkey = w->key;
		strncpy(curproc->cwdpath, w->path, MAXPATH);
	} else {
		vput(curproc->cwd);
		if (curproc->cwdkey) vput(curproc->cwdkey);
		curproc->cwd = vref(rootvn);
		curproc->cwdkey = vref(rootvn);
		strncpy(curproc->cwdpath, "/", 2);
	}
	kfree(w);
}
