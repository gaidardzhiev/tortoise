#include "kernel.h"

#define VHASH 256
#define MAXPATH 512

Vnode *rootvn;
Fs *mounts;
static Vnode *vtab[VHASH];
static unsigned boot_time;

unsigned vfs_now(void) {
	return boot_time + ticks / 100;
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

static int namex(const char *path, Vnode *cwd, int follow, int parent, Vnode **out, char *name) {
	static char bufs[2][MAXPATH];
	static int depth;
	char *buf, comp[28];
	char *p;
	int links = 0, r = 0;
	Vnode *cur;
	if (!path[0]) return -ENOENT;
	if (strlen(path) >= MAXPATH) return -ENAMETOOLONG;
	if (depth >= 2) return -ELOOP;
	buf = bufs[depth++];
	strncpy(buf, path, MAXPATH);
	p = buf;
	cur = vref(*p == '/' || !cwd ? rootvn : cwd);
	for (;;) {
		Vnode *next;
		int k = 0, last;
		while (*p == '/') p++;
		if (!*p) {
			if (parent) {
				r = -EINVAL;
				break;
			}
			*out = cur;
			depth--;
			return 0;
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
		if (curproc && (r = vfs_access(cur, 1)) != 0) break;
		if (last && parent) {
			strncpy(name, comp, 28);
			*out = cur;
			depth--;
			return 0;
		}
		if (!strcmp(comp, ".")) continue;
		if (!strcmp(comp, "..")) {
			while (cur == cur->fs->root && cur->fs->covered) {
				Vnode *c = vref(cur->fs->covered);
				vput(cur);
				cur = c;
			}
			if (cur == rootvn) continue;
		}
		r = cur->fs->ops->lookup(cur, comp, &next);
		if (r) break;
		while (next->mounted) {
			Vnode *m = vref(next->mounted->root);
			vput(next);
			next = m;
		}
		if (next->type == VT_LNK && (!last || follow)) {
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
			{
				char rest[MAXPATH];
				strncpy(rest, p, MAXPATH);
				strncpy(buf, tgt, MAXPATH);
				if (rest[0]) {
					unsigned l = strlen(buf);
					buf[l] = '/';
					strncpy(buf + l + 1, rest, MAXPATH - l - 1);
				}
			}
			p = buf;
			if (tgt[0] == '/') {
				vput(cur);
				cur = vref(rootvn);
			}
			continue;
		}
		vput(cur);
		cur = next;
	}
	vput(cur);
	depth--;
	return r;
}

int vfs_resolve(const char *path, Vnode *cwd, int follow, Vnode **out) {
	return namex(path, cwd, follow, 0, out, NULL);
}

int vfs_parent(const char *path, Vnode *cwd, Vnode **dir, char *name) {
	return namex(path, cwd, 1, 1, dir, name);
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
		target->mounted = fs;
	}
	fs->next = mounts;
	mounts = fs;
	return 0;
}

int vfs_mount(const char *source, const char *target, const char *fstype) {
	Vnode *t;
	Fs *fs;
	int r = vfs_resolve(target, cwdv(), 1, &t);
	if (r) return r;
	if (t->type != VT_DIR) {
		vput(t);
		return -ENOTDIR;
	}
	if (t->mounted) {
		vput(t);
		return -EBUSY;
	}
	fs = kmalloc(sizeof(Fs));
	if (!fs) {
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
		kfree(fs);
		vput(t);
		return r;
	}
	{
		char abs[128];
		if (target[0] == '/') strncpy(abs, target, 127);
		else {
			int n = sys_getcwd_k(abs, 120);
			if (n < 0) strncpy(abs, target, 127);
			else {
				if (strcmp(abs, "/")) strncpy(abs + strlen(abs), "/", 2);
				strncpy(abs + strlen(abs), target, 127 - strlen(abs));
			}
		}
		abs[127] = 0;
		vfs_mount_fs(fs, t, source, abs, fstype);
	}
	vput(t);
	return 0;
}

static int fs_busy(Fs *fs) {
	int i;
	Vnode *v;
	for (i = 0; i < VHASH; i++)
		for (v = vtab[i]; v; v = v->hnext)
			if (v->fs == fs && v->ref > (v == fs->root ? 2 : 0)) return 1;
	return 0;
}

int vfs_umount(const char *target) {
	Vnode *t;
	Fs *fs, **pp;
	int r = vfs_resolve(target, cwdv(), 1, &t);
	if (r) return r;
	fs = t->fs;
	if (t != fs->root || !fs->covered) {
		vput(t);
		return -EINVAL;
	}
	if (fs_busy(fs)) {
		vput(t);
		return -EBUSY;
	}
	vput(t);
	fs->covered->mounted = NULL;
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
}

int sys_getcwd_k(char *out, unsigned size) {
	char path[MAXPATH], tmp[MAXPATH];
	Vnode *v = vref(cwdv());
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
