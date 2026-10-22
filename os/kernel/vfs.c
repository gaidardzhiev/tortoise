/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define VHASH 256

Vnode *rootvn;
Fs *mounts;
static Vnode *vtab[VHASH];
static unsigned boot_time, boot_count;

unsigned uptime_ms(void) {
	return rd(TIMER_COUNT) - boot_count;
}

unsigned vfs_now(void) {
	return boot_time + uptime_ms() / 1000;
}

Vnode *vget(Fs *fs, unsigned ino, int type) {
	unsigned h = ((unsigned)fs / 16 + ino) % VHASH;
	Vnode *v;
	for (v = vtab[h]; v; v = v->hnext)
		if (v->fs == fs && v->ino == ino) {
			v->ref++;
			return v;
		}
	v = kmalloc(sizeof(Vnode));
	if (!v) return NULL;
	v->ref = 1;
	v->fs = fs;
	v->ino = ino;
	v->type = type;
	v->hnext = vtab[h];
	vtab[h] = v;
	return v;
}

Vnode *vpeek(Fs *fs, unsigned ino) {
	Vnode *v;
	for (v = vtab[((unsigned)fs / 16 + ino) % VHASH]; v; v = v->hnext)
		if (v->fs == fs && v->ino == ino) return v;
	return NULL;
}

Vnode *vref(Vnode *v) {
	v->ref++;
	return v;
}

void vput(Vnode *v) {
	unsigned h;
	Vnode **pp;
	if (!v || --v->ref > 0) return;
	if (v->fs->ops->release) v->fs->ops->release(v);
	h = ((unsigned)v->fs / 16 + v->ino) % VHASH;
	for (pp = &vtab[h]; *pp; pp = &(*pp)->hnext)
		if (*pp == v) {
			*pp = v->hnext;
			break;
		}
	kfree(v);
}

int vfs_getattr(Vnode *v, Stat2 *st) {
	memset(st, 0, sizeof(Stat2));
	if (!v->fs->ops->getattr) return -EINVAL;
	return v->fs->ops->getattr(v, st);
}

int vfs_read(Vnode *v, char *buf, unsigned off, unsigned n) {
	if (!v->fs->ops->read) return -EINVAL;
	return v->fs->ops->read(v, buf, off, n, NULL);
}

static Vnode *cwdv(void) {
	return curproc ? curproc->cwd : rootvn;
}

int vfs_mount_fs(Fs *fs, Vnode *target, const char *source, const char *tpath, const char *fstype) {
	strncpy(fs->source, source, 63);
	strncpy(fs->target, tpath, 127);
	strncpy(fs->fstype, fstype, 7);
	if (target) {
		fs->covered = vref(target);
		if (!target->mounted) target->mounted = fs;
		ns_add(ns_current(), target, tpath, fs->root, source, MFS | MCREATE);
	}
	fs->next = mounts;
	mounts = fs;
	return 0;
}

int vfs_mount(const char *source, const char *target, const char *fstype) {
	Vnode *t;
	Fs *fs;
	Walk *w = kmalloc(sizeof(Walk));
	int r = w ? vfs_walk(target, WFOLLOW | WNOMNT, w) : -ENOMEM;
	if (r) {
		kfree(w);
		return r;
	}
	t = w->key;
	vput(w->v);
	if (t->type != VT_DIR) {
		kfree(w);
		vput(t);
		return -ENOTDIR;
	}
	if (ns_is_mountpoint(t)) {
		kfree(w);
		vput(t);
		return -EBUSY;
	}
	fs = kmalloc(sizeof(Fs));
	if (!fs) {
		kfree(w);
		vput(t);
		return -ENOMEM;
	}
	if (!strcmp(fstype, "tfs")) {
		Vnode *dv;
		int disk;
		r = vfs_resolve(source, cwdv(), 1, &dv);
		if (r == 0) {
			disk = devfs_disk_of(dv);
			vput(dv);
			r = disk < 0 ? -ENODEV : tfs_mount(fs, disk);
		}
	} else if (!strcmp(fstype, "ramfs")) r = ramfs_mount(fs);
	else if (!strcmp(fstype, "dev")) r = devfs_mount(fs);
	else if (!strcmp(fstype, "proc")) r = procfs_mount(fs);
	else r = -ENODEV;
	if (r) {
		kfree(w);
		kfree(fs);
		vput(t);
		return r;
	}
	{
		char abs[128];
		strncpy(abs, w->path, 127);
		abs[127] = 0;
		vfs_mount_fs(fs, t, source, abs, fstype);
	}
	kfree(w);
	vput(t);
	return 0;
}

static int fs_busy(Fs *fs) {
	int i;
	Vnode *v;
	for (i = 0; i < VHASH; i++)
		for (v = vtab[i]; v; v = v->hnext)
			if (v->fs == fs && v->ref > (v == fs->root ? 2 + (v == rootvn) : 0) + ns_fs_refs(fs, v)) return 1;
	return 0;
}

int vfs_umount(const char *target) {
	Vnode *t;
	Fs *fs, **pp;
	int r = vfs_resolve(target, cwdv(), 1, &t);
	if (r) return r;
	fs = t->fs;
	if (t != fs->root || !fs->covered || ns_remove_fs_member(ns_current(), fs->covered, t)) {
		vput(t);
		return -EINVAL;
	}
	if (fs_busy(fs)) {
		vput(t);
		return -EBUSY;
	}
	vput(t);
	ns_drop_fs(fs);
	if (fs->covered->mounted == fs) fs->covered->mounted = NULL;
	vput(fs->covered);
	vput(fs->root);
	for (pp = &mounts; *pp; pp = &(*pp)->next)
		if (*pp == fs) {
			*pp = fs->next;
			break;
		}
	if (!strcmp(fs->fstype, "tfs")) tfs_umount(fs);
	else if (fs->priv && !strcmp(fs->fstype, "ramfs")) ramfs_umount(fs);
	kfree(fs);
	return 0;
}

static void ensure_dir(const char *name, unsigned mode) {
	Vnode *v;
	if (vfs_resolve(name, rootvn, 1, &v) == 0) {
		vput(v);
		return;
	}
	rootvn->fs->ops->mkdir(rootvn, name + 1, mode);
}

void vfs_init(void) {
	Fs *fs = kmalloc(sizeof(Fs)), *f2;
	int i;
	static const char *dirs[3] = {"/dev", "/proc", "/tmp"};
	static const char *types[3] = {"dev", "proc", "ramfs"};
	boot_time = rd(RTC_SECONDS);
	boot_count = rd(TIMER_COUNT);
	if (!fs || tfs_mount(fs, 0)) panic("cannot mount the root file system");
	rootvn = vref(fs->root);
	vfs_mount_fs(fs, NULL, "/dev/disk0", "/", "tfs");
	ensure_dir("/dev", 0755);
	ensure_dir("/proc", 0555);
	ensure_dir("/tmp", 01777);
	for (i = 0; i < 3; i++) {
		Vnode *t;
		f2 = kmalloc(sizeof(Fs));
		if (!f2 || vfs_resolve(dirs[i], rootvn, 1, &t)) panic("cannot prepare a mount point");
		if ((i == 0 ? devfs_mount(f2) : i == 1 ? procfs_mount(f2) : ramfs_mount(f2)) != 0) panic("cannot mount a system file system");
		vfs_mount_fs(f2, t, types[i], dirs[i], types[i]);
		vput(t);
	}
	dev9_init();
	srv_init();
	env_init();
	ensure_dir("/net", 0555);
	ensure_dir("/srv", 0777);
	ensure_dir("/env", 0777);
	{
		Vnode *s = env_root(), *ev;
		if (vfs_resolve("/env", rootvn, 1, &ev)) panic("cannot prepare /env");
		ns_add(ns_boot(), ev, "/env", s, "#e", MREPLACE | MCREATE);
		vput(s);
		vput(ev);
	}
	{
		Vnode *s = srv_root(), *sv;
		if (vfs_resolve("/srv", rootvn, 1, &sv)) panic("cannot prepare /srv");
		ns_add(ns_boot(), sv, "/srv", s, "#s", MREPLACE | MCREATE);
		vput(s);
		vput(sv);
	}
	{
		Vnode *t, *a = dev9_root('I'), *b = dev9_root('l');
		if (vfs_resolve("/net", rootvn, 1, &t)) panic("cannot prepare /net");
		ns_add(ns_boot(), t, "/net", a, "#I", MREPLACE);
		ns_add(ns_boot(), t, "/net", b, "#l", MAFTER);
		vput(a);
		vput(b);
		vput(t);
	}
}

int vnode_path(Vnode *start, char *out, unsigned size) {
	char path[MAXPATH], tmp[MAXPATH];
	Vnode *v = vref(start);
	path[0] = 0;
	while (v != rootvn) {
		Vnode *parent, *c;
		Stat2 st;
		Dirent de;
		unsigned off = 0;
		int found = 0;
		while (v == v->fs->root && v->fs->covered) {
			c = vref(v->fs->covered);
			vput(v);
			v = c;
		}
		if (v == rootvn) break;
		if (v->fs->ops->lookup(v, "..", &parent)) {
			vput(v);
			return -ENOENT;
		}
		while (parent->mounted) {
			c = vref(parent->mounted->root);
			vput(parent);
			parent = c;
		}
		vfs_getattr(v, &st);
		while (parent->fs->ops->readdir(parent, &off, &de) > 0) {
			if (!strcmp(de.name, ".") || !strcmp(de.name, "..")) continue;
			if (de.inode == st.ino) {
				Vnode *ch;
				if (parent->fs->ops->lookup(parent, de.name, &ch) == 0) {
					if (ch == v) found = 1;
					vput(ch);
				}
				if (found) break;
			}
		}
		if (!found) {
			vput(v);
			vput(parent);
			return -ENOENT;
		}
		if (strlen(path) + strlen(de.name) + 2 >= MAXPATH) {
			vput(v);
			vput(parent);
			return -ENAMETOOLONG;
		}
		tmp[0] = '/';
		strncpy(tmp + 1, de.name, 28);
		strncpy(tmp + strlen(tmp), path, MAXPATH - strlen(tmp));
		strncpy(path, tmp, MAXPATH);
		vput(v);
		v = parent;
	}
	vput(v);
	if (!path[0]) strncpy(path, "/", 2);
	if (strlen(path) + 1 > size) return -ERANGE;
	strncpy(out, path, size);
	return (int)strlen(path) + 1;
}

int sys_getcwd_k(char *out, unsigned size) {
	unsigned l;
	if (!curproc || !curproc->cwdpath[0]) return vnode_path(cwdv(), out, size);
	l = strlen(curproc->cwdpath) + 1;
	if (l > size) return -ERANGE;
	memcpy(out, curproc->cwdpath, l);
	return (int)l;
}

int vfs_access(Vnode *v, int want) {
	Stat2 st;
	unsigned perm;
	if (!curproc) return 0;
	if (vfs_getattr(v, &st)) return 0;
	if (curproc->euid == 0) {
		if ((want & 1) && (st.mode & S_IFMT) != S_IFDIR && !(st.mode & 0111)) return -EACCES;
		return 0;
	}
	if (st.uid == curproc->euid) perm = (st.mode >> 6) & 7;
	else if (in_group(st.gid)) perm = (st.mode >> 3) & 7;
	else perm = st.mode & 7;
	return (perm & want) == (unsigned)want ? 0 : -EACCES;
}

int vfs_access_real(Vnode *v, int want) {
	unsigned e = curproc->euid, g = curproc->egid;
	int r;
	curproc->euid = curproc->uid;
	curproc->egid = curproc->gid;
	r = vfs_access(v, want);
	curproc->euid = e;
	curproc->egid = g;
	return r;
}

int in_group(unsigned gid) {
	int i;
	if (!curproc) return 1;
	if (curproc->egid == gid) return 1;
	for (i = 0; i < curproc->ngroups; i++)
		if (curproc->groups[i] == gid) return 1;
	return 0;
}
