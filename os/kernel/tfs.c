#include "kernel.h"

#define FS_SECTOR0 1024
#define NCACHE 64
#define BSIZE 512
#define NIND (BSIZE / 4)
#define IPB 4

typedef struct {
	unsigned short type, nlink, mode, uid, gid, pad;
	unsigned size, atime, mtime, ctime;
	unsigned direct[12];
	unsigned ind, dind, tind;
	unsigned spare[10];
} TInode;

typedef struct {
	int disk;
	unsigned total, ninodes, istart, bmstart, dstart, free_blocks, free_inodes;
	unsigned bhint, ihint;
	char sb[BSIZE];
} Tfs;

typedef struct {
	int disk;
	unsigned blk;
	unsigned used;
	int valid;
	char data[BSIZE];
} CBuf;

static CBuf cache[NCACHE];
static unsigned use_clock;
static unsigned nsectors[2];
static char ibuf[BSIZE], bmbuf[BSIZE], rwbuf[BSIZE], iabuf[BSIZE], sbbuf[BSIZE];
static unsigned indbuf[NIND];

int disk_rw(int disk, int write, unsigned sector, void *buf) {
	unsigned base = disk ? 0xF0009000u : 0xF0003000u;
	if (disk < 0 || disk > 1 || sector >= nsectors[disk]) return -1;
	wr(base + 0x10, 0);
	wr(base, sector);
	wr(base + 4, 1);
	wr(base + 8, (unsigned)buf);
	wr(base + 0x0C, write ? 2 : 1);
	while (rd(base + 0x10) & 1) ;
	return (rd(base + 0x10) & 2) ? -1 : 0;
}

unsigned disk_sectors(int disk) {
	return disk >= 0 && disk <= 1 ? nsectors[disk] : 0;
}

void disk_init(void) {
	nsectors[0] = rd(DISK_SIZE);
	nsectors[1] = rd(0xF0009014u);
}

static CBuf *cache_get(int disk, unsigned blk, int load) {
	int set = (int)((blk * 2 + (unsigned)disk) % (NCACHE / 4)) * 4;
	CBuf *victim = &cache[set];
	int i;
	for (i = set; i < set + 4; i++) {
		if (cache[i].valid && cache[i].blk == blk && cache[i].disk == disk) {
			cache[i].used = ++use_clock;
			return &cache[i];
		}
		if (!cache[i].valid) victim = &cache[i];
		else if (victim->valid && cache[i].used < victim->used) victim = &cache[i];
	}
	victim->valid = 0;
	if (load && disk_rw(disk, 0, FS_SECTOR0 + blk, victim->data)) return NULL;
	victim->disk = disk;
	victim->blk = blk;
	victim->valid = 1;
	victim->used = ++use_clock;
	return victim;
}

static int bread(Tfs *t, unsigned blk, void *buf) {
	CBuf *c = cache_get(t->disk, blk, 1);
	if (!c) return -1;
	memcpy(buf, c->data, BSIZE);
	return 0;
}

static int bwrite(Tfs *t, unsigned blk, const void *buf) {
	CBuf *c = cache_get(t->disk, blk, 0);
	if (!c) return -1;
	memcpy(c->data, buf, BSIZE);
	if (disk_rw(t->disk, 1, FS_SECTOR0 + blk, c->data)) {
		c->valid = 0;
		return -1;
	}
	return 0;
}

static unsigned get32(const char *p) {
	const unsigned char *u = (const unsigned char *)p;
	return u[0] | (u[1] << 8) | (u[2] << 16) | ((unsigned)u[3] << 24);
}

static void put32(char *p, unsigned v) {
	p[0] = (char)v;
	p[1] = (char)(v >> 8);
	p[2] = (char)(v >> 16);
	p[3] = (char)(v >> 24);
}

static void sb_sync(Tfs *t) {
	put32(t->sb + 24, t->free_blocks);
	put32(t->sb + 28, t->free_inodes);
	bwrite(t, 0, t->sb);
}

static int iget(Tfs *t, unsigned ino, TInode *ip) {
	if (ino == 0 || ino >= t->ninodes) return -EINVAL;
	if (bread(t, t->istart + ino / IPB, ibuf)) return -EINVAL;
	memcpy(ip, ibuf + (ino % IPB) * 128, 128);
	return 0;
}

static int iput(Tfs *t, unsigned ino, TInode *ip) {
	if (ino == 0 || ino >= t->ninodes) return -EINVAL;
	if (bread(t, t->istart + ino / IPB, ibuf)) return -EINVAL;
	memcpy(ibuf + (ino % IPB) * 128, ip, 128);
	return bwrite(t, t->istart + ino / IPB, ibuf);
}

static unsigned balloc(Tfs *t) {
	char *buf = bmbuf;
	unsigned nb = (t->total + BSIZE * 8 - 1) / (BSIZE * 8), i, pass;
	if (t->free_blocks == 0) return 0;
	if (t->bhint < t->dstart || t->bhint >= t->total) t->bhint = t->dstart;
	for (pass = 0; pass <= nb; pass++) {
		unsigned j;
		i = (t->bhint / (BSIZE * 8) + pass) % nb;
		if (bread(t, t->bmstart + i, buf)) return 0;
		for (j = pass == 0 ? t->bhint % (BSIZE * 8) : 0; j < BSIZE * 8; j++) {
			unsigned b = i * BSIZE * 8 + j;
			if (b >= t->total) break;
			if (b < t->dstart) continue;
			if (!(buf[j / 8] & (1 << (j % 8)))) {
				buf[j / 8] |= (char)(1 << (j % 8));
				if (bwrite(t, t->bmstart + i, buf)) return 0;
				memset(buf, 0, BSIZE);
				if (bwrite(t, b, buf)) return 0;
				t->free_blocks--;
				sb_sync(t);
				t->bhint = b + 1;
				return b;
			}
		}
	}
	return 0;
}

static void bfree(Tfs *t, unsigned b) {
	char *buf = bmbuf;
	unsigned i = b / (BSIZE * 8), j = b % (BSIZE * 8);
	if (b < t->dstart || b >= t->total) return;
	if (bread(t, t->bmstart + i, buf)) return;
	if (!(buf[j / 8] & (1 << (j % 8)))) return;
	buf[j / 8] &= (char)~(1 << (j % 8));
	bwrite(t, t->bmstart + i, buf);
	t->free_blocks++;
	sb_sync(t);
}

static unsigned ind_entry(Tfs *t, unsigned *slot, unsigned idx, int alloc, int *dirty) {
	unsigned *buf = indbuf;
	unsigned v;
	if (!*slot) {
		if (!alloc) return 0;
		*slot = balloc(t);
		if (!*slot) return 0;
		*dirty = 1;
	}
	if (bread(t, *slot, buf)) return 0;
	v = buf[idx];
	if (!v && alloc) {
		v = balloc(t);
		if (!v) return 0;
		if (bread(t, *slot, buf)) return 0;
		buf[idx] = v;
		if (bwrite(t, *slot, buf)) return 0;
	}
	return v;
}

static unsigned bmap(Tfs *t, unsigned ino, TInode *ip, unsigned fb, int alloc) {
	int dirty = 0;
	unsigned r = 0;
	if (fb < 12) {
		r = ip->direct[fb];
		if (!r && alloc) {
			r = balloc(t);
			ip->direct[fb] = r;
			dirty = 1;
		}
	} else if (fb < 12 + NIND) {
		r = ind_entry(t, &ip->ind, fb - 12, alloc, &dirty);
	} else if (fb < 12 + NIND + NIND * NIND) {
		unsigned k = fb - 12 - NIND, mid;
		mid = ind_entry(t, &ip->dind, k / NIND, alloc, &dirty);
		if (mid) r = ind_entry(t, &mid, k % NIND, alloc, &dirty);
	} else {
		unsigned k = fb - 12 - NIND - NIND * NIND, a, b2;
		if (k >= NIND * NIND * NIND) return 0;
		a = ind_entry(t, &ip->tind, k / (NIND * NIND), alloc, &dirty);
		if (a) {
			b2 = ind_entry(t, &a, (k / NIND) % NIND, alloc, &dirty);
			if (b2) r = ind_entry(t, &b2, k % NIND, alloc, &dirty);
		}
	}
	if (dirty) iput(t, ino, ip);
	return r;
}

static int readi(Tfs *t, unsigned ino, char *dst, unsigned off, unsigned n) {
	TInode di;
	unsigned done = 0;
	char *buf = rwbuf;
	if (iget(t, ino, &di)) return -EINVAL;
	if (off >= di.size) return 0;
	if (off + n > di.size || off + n < off) n = di.size - off;
	while (done < n) {
		unsigned b = bmap(t, ino, &di, off / BSIZE, 0), o = off % BSIZE, k = BSIZE - o;
		if (k > n - done) k = n - done;
		if (b) {
			if (bread(t, b, buf)) return -EINVAL;
			memcpy(dst + done, buf + o, k);
		} else {
			memset(dst + done, 0, k);
		}
		done += k;
		off += k;
	}
	return (int)done;
}

static int writei(Tfs *t, unsigned ino, const char *src, unsigned off, unsigned n) {
	TInode di;
	unsigned done = 0;
	char *buf = rwbuf;
	if (iget(t, ino, &di)) return -EINVAL;
	while (done < n) {
		unsigned b = bmap(t, ino, &di, off / BSIZE, 1), o = off % BSIZE, k = BSIZE - o;
		if (!b) break;
		if (k > n - done) k = n - done;
		if (k < BSIZE && bread(t, b, buf)) break;
		memcpy(buf + o, src + done, k);
		if (bwrite(t, b, buf)) break;
		done += k;
		off += k;
	}
	if (off > di.size) di.size = off;
	di.mtime = di.ctime = vfs_now();
	iput(t, ino, &di);
	if (done < n && done == 0) return -ENOSPC;
	return (int)done;
}

static int free_tree(Tfs *t, unsigned b, int level, unsigned first) {
	unsigned buf[NIND];
	unsigned span = 1, i, keep = 0;
	int l;
	if (!b) return 0;
	for (l = 1; l < level; l++) span *= NIND;
	if (bread(t, b, buf)) return 1;
	for (i = 0; i < NIND; i++) {
		unsigned lo = i * span;
		if (!buf[i]) continue;
		if (lo + span <= first) {
			keep = 1;
			continue;
		}
		if (level > 1) {
			if (free_tree(t, buf[i], level - 1, first > lo ? first - lo : 0)) {
				keep = 1;
				continue;
			}
		} else if (lo < first) {
			keep = 1;
			continue;
		} else {
			bfree(t, buf[i]);
		}
		buf[i] = 0;
	}
	if (keep) {
		bwrite(t, b, buf);
		return 1;
	}
	bfree(t, b);
	return 0;
}

static int itrunc(Tfs *t, unsigned ino, unsigned len) {
	TInode di;
	unsigned first = (len + BSIZE - 1) / BSIZE, i;
	if (iget(t, ino, &di)) return -EINVAL;
	if (len >= di.size) {
		di.size = len;
		di.mtime = di.ctime = vfs_now();
		return iput(t, ino, &di);
	}
	for (i = first; i < 12; i++) {
		if (di.direct[i]) bfree(t, di.direct[i]);
		di.direct[i] = 0;
	}
	if (!free_tree(t, di.ind, 1, first > 12 ? first - 12 : 0)) di.ind = 0;
	if (!free_tree(t, di.dind, 2, first > 12 + NIND ? first - 12 - NIND : 0)) di.dind = 0;
	if (!free_tree(t, di.tind, 3, first > 12 + NIND + NIND * NIND ? first - 12 - NIND - NIND * NIND : 0)) di.tind = 0;
	if (len % BSIZE && first > 0) {
		unsigned b = bmap(t, ino, &di, first - 1, 0);
		if (b && bread(t, b, rwbuf) == 0) {
			memset(rwbuf + len % BSIZE, 0, BSIZE - len % BSIZE);
			bwrite(t, b, rwbuf);
		}
	}
	di.size = len;
	di.mtime = di.ctime = vfs_now();
	return iput(t, ino, &di);
}

static int ialloc(Tfs *t, int type, unsigned mode) {
	char *buf = iabuf;
	unsigned ino;
	if (t->free_inodes == 0) return -ENOSPC;
	unsigned k;
	if (t->ihint < 1 || t->ihint >= t->ninodes) t->ihint = 1;
	for (k = 0; k < t->ninodes; k++) {
		TInode *ip;
		ino = (t->ihint + k) % t->ninodes;
		if (ino == 0) continue;
		if (ino % IPB == 0 || k == 0 || ino == 1) {
			if (bread(t, t->istart + ino / IPB, buf)) return -ENOSPC;
		}
		ip = (TInode *)(buf + (ino % IPB) * 128);
		if (ip->type == 0) {
			unsigned now = vfs_now();
			memset(ip, 0, 128);
			ip->type = (unsigned short)type;
			ip->nlink = 1;
			ip->mode = (unsigned short)(mode & 07777);
			ip->uid = (unsigned short)(curproc ? curproc->euid : 0);
			ip->gid = (unsigned short)(curproc ? curproc->egid : 0);
			ip->atime = ip->mtime = ip->ctime = now;
			if (bwrite(t, t->istart + ino / IPB, buf)) return -ENOSPC;
			t->free_inodes--;
			sb_sync(t);
			t->ihint = ino + 1;
			return (int)ino;
		}
	}
	return -ENOSPC;
}

static void ifree(Tfs *t, unsigned ino) {
	TInode di;
	itrunc(t, ino, 0);
	if (iget(t, ino, &di)) return;
	memset(&di, 0, sizeof(di));
	iput(t, ino, &di);
	t->free_inodes++;
	sb_sync(t);
}

static int dirlookup(Tfs *t, unsigned dir, const char *name, unsigned *poff) {
	TInode di;
	Dirent de;
	unsigned off;
	if (iget(t, dir, &di) || di.type != 2) return -ENOTDIR;
	for (off = 0; off < di.size; off += 32) {
		if (readi(t, dir, (char *)&de, off, 32) != 32) break;
		if (de.inode && strncmp(de.name, name, 27) == 0) {
			if (poff) *poff = off;
			return (int)de.inode;
		}
	}
	return -ENOENT;
}

static int dirlink(Tfs *t, unsigned dir, const char *name, unsigned ino) {
	TInode di;
	Dirent de;
	unsigned off;
	if (strlen(name) > 27) return -ENAMETOOLONG;
	if (iget(t, dir, &di)) return -EINVAL;
	for (off = 0; off < di.size; off += 32) {
		if (readi(t, dir, (char *)&de, off, 32) != 32) break;
		if (!de.inode) break;
	}
	memset(&de, 0, sizeof(de));
	de.inode = ino;
	strncpy(de.name, name, 27);
	if (writei(t, dir, (char *)&de, off, 32) != 32) return -ENOSPC;
	return 0;
}

static void dirclear(Tfs *t, unsigned dir, unsigned off) {
	Dirent de;
	memset(&de, 0, sizeof(de));
	writei(t, dir, (char *)&de, off, 32);
}

static int dir_empty(Tfs *t, unsigned dir) {
	TInode di;
	Dirent de;
	unsigned off;
	if (iget(t, dir, &di)) return 0;
	for (off = 0; off < di.size; off += 32) {
		if (readi(t, dir, (char *)&de, off, 32) != 32) break;
		if (de.inode && strcmp(de.name, ".") && strcmp(de.name, "..")) return 0;
	}
	return 1;
}

static int vtype_of(int t) {
	return t == 2 ? VT_DIR : t == 3 ? VT_LNK : t == 4 ? VT_SOCK : t == 5 ? VT_FIFO : VT_REG;
}

static Tfs *T(Vnode *v) {
	return v->fs->priv;
}

static int t_lookup(Vnode *dir, const char *name, Vnode **out) {
	Tfs *t = T(dir);
	TInode di;
	int ino = dirlookup(t, dir->ino, name, NULL);
	if (ino < 0) return ino;
	if (iget(t, (unsigned)ino, &di)) return -EINVAL;
	*out = vget(dir->fs, (unsigned)ino, vtype_of(di.type));
	return *out ? 0 : -ENOMEM;
}

static int t_make(Vnode *dir, const char *name, int type, unsigned mode, int *pino) {
	Tfs *t = T(dir);
	int ino, r;
	if (strlen(name) > 27) return -ENAMETOOLONG;
	if (dirlookup(t, dir->ino, name, NULL) >= 0) return -EEXIST;
	ino = ialloc(t, type, mode);
	if (ino < 0) return ino;
	if (curproc && type) {
		TInode pd, di;
		if (iget(t, dir->ino, &pd) == 0 && (pd.mode & 02000)) {
			iget(t, (unsigned)ino, &di);
			di.gid = pd.gid;
			if (type == 2) di.mode |= 02000;
			iput(t, (unsigned)ino, &di);
		}
	}
	r = dirlink(t, dir->ino, name, (unsigned)ino);
	if (r) {
		ifree(t, (unsigned)ino);
		return r;
	}
	*pino = ino;
	return 0;
}

static int t_create(Vnode *dir, const char *name, unsigned mode, Vnode **out) {
	int ino, r = t_make(dir, name, 1, mode, &ino);
	if (r) return r;
	*out = vget(dir->fs, (unsigned)ino, VT_REG);
	return *out ? 0 : -ENOMEM;
}

static int t_mknod(Vnode *dir, const char *name, unsigned mode, Vnode **out) {
	int ino, r = t_make(dir, name, (mode & S_IFMT) == S_IFSOCK ? 4 : 5, mode, &ino);
	if (r) return r;
	*out = vget(dir->fs, (unsigned)ino, (mode & S_IFMT) == S_IFSOCK ? VT_SOCK : VT_FIFO);
	return *out ? 0 : -ENOMEM;
}

static int t_mkdir(Vnode *dir, const char *name, unsigned mode) {
	Tfs *t = T(dir);
	int ino, r = t_make(dir, name, 2, mode, &ino);
	TInode di;
	if (r) return r;
	if (dirlink(t, (unsigned)ino, ".", (unsigned)ino) || dirlink(t, (unsigned)ino, "..", dir->ino)) return -ENOSPC;
	iget(t, (unsigned)ino, &di);
	di.nlink = 2;
	iput(t, (unsigned)ino, &di);
	iget(t, dir->ino, &di);
	di.nlink++;
	iput(t, dir->ino, &di);
	return 0;
}

static void drop_link(Vnode *dir, unsigned ino) {
	Tfs *t = T(dir);
	TInode di;
	Vnode *v;
	if (iget(t, ino, &di)) return;
	if (di.nlink) di.nlink--;
	di.ctime = vfs_now();
	iput(t, ino, &di);
	if (di.nlink) return;
	v = vget(dir->fs, ino, vtype_of(di.type));
	if (v && v->ref > 1) {
		v->nlink_zero = 1;
		vput(v);
		return;
	}
	if (v) {
		v->nlink_zero = 0;
		vput(v);
	}
	ifree(t, ino);
}

static int t_unlink(Vnode *dir, const char *name) {
	Tfs *t = T(dir);
	unsigned off;
	TInode di;
	int ino = dirlookup(t, dir->ino, name, &off);
	if (ino < 0) return ino;
	if (iget(t, (unsigned)ino, &di)) return -EINVAL;
	if (di.type == 2) return -EISDIR;
	dirclear(t, dir->ino, off);
	drop_link(dir, (unsigned)ino);
	return 0;
}

static int t_rmdir(Vnode *dir, const char *name) {
	Tfs *t = T(dir);
	unsigned off;
	TInode di;
	int ino;
	if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
	ino = dirlookup(t, dir->ino, name, &off);
	if (ino < 0) return ino;
	if (iget(t, (unsigned)ino, &di)) return -EINVAL;
	if (di.type != 2) return -ENOTDIR;
	if (!dir_empty(t, (unsigned)ino)) return -ENOTEMPTY;
	dirclear(t, dir->ino, off);
	iget(t, dir->ino, &di);
	di.nlink--;
	iput(t, dir->ino, &di);
	iget(t, (unsigned)ino, &di);
	di.nlink = 1;
	iput(t, (unsigned)ino, &di);
	drop_link(dir, (unsigned)ino);
	return 0;
}

static int t_rename(Vnode *odir, const char *on, Vnode *ndir, const char *nn) {
	Tfs *t = T(odir);
	unsigned off, noff;
	TInode si, ti;
	int sino = dirlookup(t, odir->ino, on, &off), tino;
	if (sino < 0) return sino;
	if (strlen(nn) > 27) return -ENAMETOOLONG;
	if (iget(t, (unsigned)sino, &si)) return -EINVAL;
	tino = dirlookup(t, ndir->ino, nn, &noff);
	if (tino == sino) return 0;
	if (tino >= 0) {
		if (iget(t, (unsigned)tino, &ti)) return -EINVAL;
		if (ti.type == 2) {
			int r;
			if (si.type != 2) return -EISDIR;
			r = t_rmdir(ndir, nn);
			if (r) return r;
		} else {
			if (si.type == 2) return -ENOTDIR;
			t_unlink(ndir, nn);
		}
	}
	if (dirlink(t, ndir->ino, nn, (unsigned)sino)) return -ENOSPC;
	if (dirlookup(t, odir->ino, on, &off) >= 0) dirclear(t, odir->ino, off);
	if (si.type == 2 && odir->ino != ndir->ino) {
		unsigned doff;
		TInode d;
		if (dirlookup(t, (unsigned)sino, "..", &doff) >= 0) dirclear(t, (unsigned)sino, doff);
		dirlink(t, (unsigned)sino, "..", ndir->ino);
		iget(t, odir->ino, &d);
		d.nlink--;
		iput(t, odir->ino, &d);
		iget(t, ndir->ino, &d);
		d.nlink++;
		iput(t, ndir->ino, &d);
	}
	iget(t, (unsigned)sino, &si);
	si.ctime = vfs_now();
	iput(t, (unsigned)sino, &si);
	return 0;
}

static int t_link(Vnode *dir, const char *name, Vnode *target) {
	Tfs *t = T(dir);
	TInode di;
	int r;
	if (target->type == VT_DIR) return -EPERM;
	if (dirlookup(t, dir->ino, name, NULL) >= 0) return -EEXIST;
	r = dirlink(t, dir->ino, name, target->ino);
	if (r) return r;
	iget(t, target->ino, &di);
	di.nlink++;
	di.ctime = vfs_now();
	iput(t, target->ino, &di);
	return 0;
}

static int t_symlink(Vnode *dir, const char *name, const char *target) {
	int ino, r = t_make(dir, name, 3, 0777, &ino);
	if (r) return r;
	r = writei(T(dir), (unsigned)ino, target, 0, strlen(target));
	return r < 0 ? r : 0;
}

static int t_readlink(Vnode *v, char *buf, unsigned n) {
	return readi(T(v), v->ino, buf, 0, n);
}

static int t_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	(void)f;
	return readi(T(v), v->ino, buf, off, n);
}

static int t_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	(void)f;
	return writei(T(v), v->ino, buf, off, n);
}

static int t_readdir(Vnode *v, unsigned *off, Dirent *de) {
	Tfs *t = T(v);
	TInode di;
	if (iget(t, v->ino, &di) || di.type != 2) return -ENOTDIR;
	while (*off + 32 <= di.size) {
		if (readi(t, v->ino, (char *)de, *off, 32) != 32) return 0;
		*off += 32;
		if (de->inode) {
			de->name[27] = 0;
			return 1;
		}
	}
	return 0;
}

static int t_getattr(Vnode *v, Stat2 *st) {
	TInode di;
	static const unsigned tbits[6] = {0, S_IFREG, S_IFDIR, S_IFLNK, S_IFSOCK, S_IFIFO};
	if (iget(T(v), v->ino, &di)) return -EINVAL;
	st->dev = v->fs->dev;
	st->ino = v->ino;
	st->mode = tbits[di.type < 6 ? di.type : 1] | di.mode;
	st->nlink = di.nlink;
	st->uid = di.uid;
	st->gid = di.gid;
	st->size = di.size;
	st->atime = di.atime;
	st->mtime = di.mtime;
	st->ctime = di.ctime;
	st->blocks = (di.size + 511) / 512;
	return 0;
}

static int t_setattr(Vnode *v, int what, unsigned a, unsigned b) {
	TInode di;
	if (iget(T(v), v->ino, &di)) return -EINVAL;
	if (what == SA_MODE) di.mode = (unsigned short)(a & 07777);
	else if (what == SA_OWNER) {
		if (a != 0xFFFFFFFFu) di.uid = (unsigned short)a;
		if (b != 0xFFFFFFFFu) di.gid = (unsigned short)b;
	} else if (what == SA_TIMES) {
		di.atime = a;
		di.mtime = b;
	}
	di.ctime = vfs_now();
	return iput(T(v), v->ino, &di);
}

static int t_truncate(Vnode *v, unsigned len) {
	return itrunc(T(v), v->ino, len);
}

static void t_release(Vnode *v) {
	if (v->nlink_zero) ifree(T(v), v->ino);
}

FsOps tfs_ops = {
	t_lookup, t_create, t_mkdir, t_unlink, t_rmdir, t_rename, t_link, t_symlink, t_readlink,
	t_read, t_write, t_readdir, t_getattr, t_setattr, t_truncate, NULL, NULL, t_release, t_mknod
};

int tfs_mount(Fs *fs, int disk) {
	Tfs *t;
	if (disk_sectors(disk) <= FS_SECTOR0) return -ENODEV;
	t = kmalloc(sizeof(Tfs));
	if (!t) return -ENOMEM;
	t->disk = disk;
	if (disk_rw(disk, 0, FS_SECTOR0, sbbuf) || memcmp(sbbuf, "TFS2", 4)) {
		kfree(t);
		return -EINVAL;
	}
	t->total = get32(sbbuf + 4);
	t->ninodes = get32(sbbuf + 8);
	t->istart = get32(sbbuf + 12);
	t->bmstart = get32(sbbuf + 16);
	t->dstart = get32(sbbuf + 20);
	t->free_blocks = get32(sbbuf + 24);
	t->free_inodes = get32(sbbuf + 28);
	memcpy(t->sb, sbbuf, BSIZE);
	fs->priv = t;
	fs->ops = &tfs_ops;
	fs->dev = (8u << 8) | (unsigned)disk;
	fs->root = vget(fs, 1, VT_DIR);
	return fs->root ? 0 : -ENOMEM;
}

void tfs_invalidate(int disk) {
	int i;
	for (i = 0; i < NCACHE; i++)
		if (cache[i].disk == disk) cache[i].valid = 0;
}

void tfs_umount(Fs *fs) {
	Tfs *t = fs->priv;
	tfs_invalidate(t->disk);
	kfree(t);
}
