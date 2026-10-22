/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define NDEV 12
#define D_CONSOLE 0
#define D_TTY 1
#define D_NULL 2
#define D_ZERO 3
#define D_RANDOM 4
#define D_URANDOM 5
#define D_KMSG 6
#define D_DISK0 7
#define D_DISK1 8
#define D_FB0 9
#define D_KBD 10
#define D_CONSCTL 11

typedef struct {
	const char *name;
	unsigned major, minor;
	unsigned mode, uid, gid, atime, mtime, ctime;
} Dev;

static Dev devs[NDEV] = {
	{"console", 4, 1, 0666, 0, 0, 0, 0, 0},
	{"tty", 4, 0, 0666, 0, 0, 0, 0, 0},
	{"null", 1, 3, 0666, 0, 0, 0, 0, 0},
	{"zero", 1, 5, 0666, 0, 0, 0, 0, 0},
	{"random", 1, 8, 0666, 0, 0, 0, 0, 0},
	{"urandom", 1, 9, 0666, 0, 0, 0, 0, 0},
	{"kmsg", 1, 11, 0600, 0, 0, 0, 0, 0},
	{"disk0", 8, 0, 0600, 0, 0, 0, 0, 0},
	{"disk1", 8, 1, 0600, 0, 0, 0, 0, 0},
	{"fb0", 29, 0, 0660, 0, 0, 0, 0, 0},
	{"kbd", 13, 0, 0660, 0, 0, 0, 0, 0},
	{"consctl", 4, 2, 0666, 0, 0, 0, 0, 0},
};

static unsigned rs[4];
static unsigned devfs_dev;
static char secbuf[512];

static unsigned xrand(void) {
	unsigned t = rs[3], s = rs[0];
	rs[3] = rs[2];
	rs[2] = rs[1];
	rs[1] = s;
	t ^= t << 11;
	t ^= t >> 8;
	rs[0] = t ^ s ^ (s >> 19);
	return rs[0];
}

unsigned kernel_random(void) {
	return xrand();
}

void random_mix(unsigned v) {
	rs[0] ^= v;
	rs[2] ^= v * 2654435761u;
	xrand();
}

static int dev_index(Vnode *v) {
	return v->ino >= 2 && v->ino < 2 + NDEV ? (int)v->ino - 2 : -1;
}

int devfs_disk_of(Vnode *v) {
	int i;
	if (v->fs->ops != &devfs_ops) return -1;
	i = dev_index(v);
	if (i == D_DISK0) return 0;
	if (i == D_DISK1) return 1;
	return -1;
}

static int d_lookup(Vnode *dir, const char *name, Vnode **out) {
	int i;
	if (!strcmp(name, "..")) {
		*out = vref(dir->fs->root);
		return 0;
	}
	if (!strcmp(name, ".")) {
		*out = vref(dir);
		return 0;
	}
	if (dir->ino != 1) return pty_lookup(dir, name, out);
	for (i = 0; i < NDEV; i++)
		if (!strcmp(devs[i].name, name)) {
			*out = vget(dir->fs, (unsigned)i + 2, VT_CHR);
			return *out ? 0 : -ENOMEM;
		}
	return pty_lookup(dir, name, out);
}

static int disk_io(int disk, int write, char *buf, unsigned off, unsigned n) {
	unsigned total = disk_sectors(disk) * 512, done = 0;
	if (off >= total) return 0;
	if (off + n > total) n = total - off;
	while (done < n) {
		unsigned sec = (off + done) / 512, o = (off + done) % 512, k = 512 - o;
		if (k > n - done) k = n - done;
		if (disk_rw(disk, 0, sec, secbuf)) return done ? (int)done : -EINVAL;
		if (write) {
			memcpy(secbuf + o, buf + done, k);
			if (disk_rw(disk, 1, sec, secbuf)) return done ? (int)done : -EINVAL;
		} else {
			memcpy(buf + done, secbuf + o, k);
		}
		done += k;
	}
	tfs_invalidate(disk);
	return (int)done;
}

#define FB_SIZE (640 * 480 * 4)

static unsigned fb_pa;

static int fb_io(int write, char *buf, unsigned off, unsigned n) {
	unsigned done = 0;
	if (off >= FB_SIZE) return 0;
	if (n > FB_SIZE - off) n = FB_SIZE - off;
	if (!fb_pa && rd(0xF0006000u)) {
		fb_pa = frame_alloc_contig(FB_SIZE / PAGE);
		if (fb_pa) {
			wr(0xF0006008u, fb_pa);
			wr(0xF000600Cu, 1);
		}
	}
	if (!fb_pa) {
		if (!write) memset(buf, 0, n);
		return (int)n;
	}
	while (done < n) {
		unsigned o = (off + done) % PAGE, k = PAGE - o;
		char *p;
		if (k > n - done) k = n - done;
		p = (char *)kmap(4, fb_pa + ((off + done) / PAGE) * PAGE) + o;
		if (write) memcpy(p, buf + done, k);
		else memcpy(buf + done, p, k);
		done += k;
	}
	return (int)n;
}

int devfs_is_console(Vnode *v) {
	int i;
	if (v->fs->ops != &devfs_ops) return 0;
	i = dev_index(v);
	return i == D_CONSOLE || (i == D_TTY && curproc && curproc->ctty == &contty);
}

int devfs_is_fb(Vnode *v) {
	return v->fs->ops == &devfs_ops && dev_index(v) == D_FB0;
}

static Tty *dev_tty(Vnode *v) {
	return dev_index(v) == D_CONSOLE ? &contty : curproc ? curproc->ctty : NULL;
}

static int d_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	unsigned i;
	switch (dev_index(v)) {
	case D_CONSOLE:
	case D_TTY:
		return tty_read(dev_tty(v), buf, n, f);
	case D_NULL:
		return 0;
	case D_ZERO:
		memset(buf, 0, n);
		return (int)n;
	case D_RANDOM:
	case D_URANDOM:
		for (i = 0; i < n; i++) buf[i] = (char)(xrand() >> 24);
		return (int)n;
	case D_KMSG:
		return klog_read(buf, off, n);
	case D_DISK0:
		return disk_io(0, 0, buf, off, n);
	case D_DISK1:
		return disk_io(1, 0, buf, off, n);
	case D_FB0:
		return fb_io(0, buf, off, n);
	case D_KBD:
		for (i = 0; i < n && (rd(0xF0004004u) & 1); i++) buf[i] = (char)rd(0xF0004000u);
		return (int)i;
	case D_CONSCTL:
		return 0;
	}
	return pty_read(v, buf, n, f);
}

static int d_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	unsigned i;
	switch (dev_index(v)) {
	case D_CONSOLE:
	case D_TTY:
		return tty_write(dev_tty(v), buf, n, f);
	case D_KMSG:
		for (i = 0; i < n; i++) klog_putc(buf[i]);
		return (int)n;
	case D_DISK0:
		return disk_io(0, 1, (char *)buf, off, n);
	case D_DISK1:
		return disk_io(1, 1, (char *)buf, off, n);
	case D_NULL:
	case D_ZERO:
	case D_RANDOM:
	case D_URANDOM:
	case D_FB0:
		return fb_io(1, (char *)buf, off, n);
	case D_KBD:
		return (int)n;
	case D_CONSCTL: {
		char cmd[ERRMAX];
		int r = ctl_text(cmd, buf, n);
		if (r < 0) return r;
		r = tty_ctl(&contty, cmd);
		return r < 0 ? r : (int)n;
	}
	}
	return pty_write(v, buf, n, f);
}

static int d_readdir(Vnode *v, unsigned *off, Dirent *de) {
	if (v->ino != 1) return pty_readdir(v, off, de, 0);
	if (*off == 0) {
		de->inode = 1;
		strncpy(de->name, ".", 28);
		*off = 1;
		return 1;
	}
	if (*off == 1) {
		de->inode = 1;
		strncpy(de->name, "..", 28);
		*off = 2;
		return 1;
	}
	if (*off - 2 < NDEV) {
		de->inode = *off;
		strncpy(de->name, devs[*off - 2].name, 28);
		(*off)++;
		return 1;
	}
	return pty_readdir(v, off, de, NDEV + 2);
}

static int d_getattr(Vnode *v, Stat2 *st) {
	int i = dev_index(v);
	st->dev = v->fs->dev;
	st->ino = v->ino;
	st->nlink = 1;
	if (v->ino == 1) {
		st->mode = S_IFDIR | 0755;
		st->nlink = 2;
		return 0;
	}
	if (i < 0) return pty_getattr(v, st);
	st->mode = S_IFCHR | devs[i].mode;
	st->uid = devs[i].uid;
	st->gid = devs[i].gid;
	st->atime = devs[i].atime;
	st->mtime = devs[i].mtime;
	st->ctime = devs[i].ctime;
	st->size = (i == D_DISK0 || i == D_DISK1) ? disk_sectors(i - D_DISK0) * 512 : i == D_FB0 ? FB_SIZE : 0;
	st->dev = (devs[i].major << 8) | devs[i].minor;
	return 0;
}

static int d_setattr(Vnode *v, int what, unsigned a, unsigned b) {
	int i = dev_index(v);
	if (i < 0) return v->ino == 1 ? -EPERM : 0;
	if (what == SA_MODE) devs[i].mode = a & 07777;
	else if (what == SA_OWNER) {
		if (a != 0xFFFFFFFFu) devs[i].uid = a;
		if (b != 0xFFFFFFFFu) devs[i].gid = b;
	} else if (what == SA_TIMES) {
		devs[i].atime = a;
		devs[i].mtime = b;
	}
	devs[i].ctime = vfs_now();
	return 0;
}

static int d_ioctl(Vnode *v, unsigned req, unsigned arg, File *f) {
	int i = dev_index(v);
	if (i == D_CONSOLE || i == D_TTY) return tty_ioctl(dev_tty(v), req, arg, f);
	if (i < 0) return pty_ioctl(v, req, arg, f);
	return -ENOTTY;
}

static int d_poll(Vnode *v, File *f) {
	int i = dev_index(v);
	if (i == D_CONSOLE || i == D_TTY) return tty_poll(dev_tty(v), f);
	if (i < 0) return pty_poll(v, f);
	return POLLIN | POLLOUT;
}

static int d_truncate(Vnode *v, unsigned len) {
	(void)v;
	(void)len;
	return 0;
}

FsOps devfs_ops = {
	d_lookup, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	d_read, d_write, d_readdir, d_getattr, d_setattr, d_truncate, d_ioctl, d_poll, NULL, NULL
};

int devfs_mount(Fs *fs) {
	unsigned i, now = vfs_now();
	rs[0] = rd(RTC_SECONDS) ^ 0x12345678u;
	rs[1] = 0x9E3779B9u;
	rs[2] = 0x243F6A88u;
	rs[3] = 0xB7E15162u;
	for (i = 0; i < 16; i++) xrand();
	for (i = 0; i < NDEV; i++) devs[i].atime = devs[i].mtime = devs[i].ctime = now;
	fs->ops = &devfs_ops;
	fs->dev = (3u << 8) | devfs_dev++;
	fs->root = vget(fs, 1, VT_DIR);
	return fs->root ? 0 : -ENOMEM;
}
