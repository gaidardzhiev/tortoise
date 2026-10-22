/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define NPTY 8
#define OBUF 4096
#define INO_PTMX 100
#define INO_PTS 101
#define INO_SLAVE 200
#define INO_CTL 300
#define TIOCGPTN 0x80045430u

typedef struct Pty {
	int used, master, slaves, opened;
	Tty t;
	char out[OBUF];
	unsigned oh, ol;
	WaitQueue mwq;
} Pty;

static Pty ptys[NPTY];

static void pty_out(Tty *t, int c) {
	Pty *p = t->priv;
	if (!p->master || p->ol >= OBUF) return;
	p->out[(p->oh + p->ol) % OBUF] = (char)c;
	p->ol++;
	wakeup(&p->mwq);
	wakeup(&poll_wq);
}

static Pty *slave_of(Vnode *v) {
	unsigned n;
	if (v->ino < INO_SLAVE || v->ino >= INO_SLAVE + NPTY) return NULL;
	n = v->ino - INO_SLAVE;
	return ptys[n].used ? &ptys[n] : NULL;
}

int pty_lookup(Vnode *dir, const char *name, Vnode **out) {
	if (dir->ino == 1) {
		if (!strcmp(name, "ptmx")) *out = vget(dir->fs, INO_PTMX, VT_CHR);
		else if (!strcmp(name, "pts")) *out = vget(dir->fs, INO_PTS, VT_DIR);
		else return -ENOENT;
		return *out ? 0 : -ENOMEM;
	}
	if (dir->ino == INO_PTS) {
		unsigned n = 0, l = strlen(name), ctl = l > 3 && !strcmp(name + l - 3, "ctl");
		const char *s = name;
		if (!*s || (ctl && l == 3)) return -ENOENT;
		for (; *s && (!ctl || s < name + l - 3); s++) {
			if (*s < '0' || *s > '9') return -ENOENT;
			n = n * 10 + (unsigned)(*s - '0');
		}
		if (n >= NPTY || !ptys[n].used) return -ENOENT;
		*out = vget(dir->fs, (ctl ? INO_CTL : INO_SLAVE) + n, ctl ? VT_REG : VT_CHR);
		return *out ? 0 : -ENOMEM;
	}
	return -ENOENT;
}

int pty_readdir(Vnode *v, unsigned *off, Dirent *de, unsigned first) {
	if (v->ino == 1) {
		if (*off == first) {
			de->inode = INO_PTMX;
			strncpy(de->name, "ptmx", 28);
			(*off)++;
			return 1;
		}
		if (*off == first + 1) {
			de->inode = INO_PTS;
			strncpy(de->name, "pts", 28);
			(*off)++;
			return 1;
		}
		return 0;
	}
	if (v->ino != INO_PTS) return 0;
	if (*off == 0) {
		de->inode = INO_PTS;
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
	while (*off - 2 < 2 * NPTY) {
		unsigned k = *off - 2, n = k / 2;
		(*off)++;
		if (ptys[n].used) {
			de->inode = (k & 1 ? INO_CTL : INO_SLAVE) + n;
			ksnprintf(de->name, 28, k & 1 ? "%uctl" : "%u", n);
			return 1;
		}
	}
	return 0;
}

int pty_getattr(Vnode *v, Stat2 *st) {
	Pty *p;
	st->atime = st->mtime = st->ctime = vfs_now();
	if (v->ino == INO_PTMX) {
		st->mode = S_IFCHR | 0666;
		st->dev = (5u << 8) | 2;
		return 0;
	}
	if (v->ino == INO_PTS) {
		st->mode = S_IFDIR | 0755;
		st->nlink = 2;
		return 0;
	}
	if (v->ino >= INO_CTL && v->ino < INO_CTL + NPTY) {
		if (!ptys[v->ino - INO_CTL].used) return -ENOENT;
		st->mode = S_IFREG | 0620;
		return 0;
	}
	p = slave_of(v);
	if (!p) return -ENOENT;
	st->mode = S_IFCHR | 0620;
	st->dev = (136u << 8) | (v->ino - INO_SLAVE);
	return 0;
}

int pty_open(Vnode *v, File *f) {
	int i;
	Pty *p;
	if (v->ino == INO_PTMX) {
		for (i = 0; i < NPTY; i++)
			if (!ptys[i].used) {
				p = &ptys[i];
				memset(p, 0, sizeof(Pty));
				p->used = 1;
				p->master = 1;
				tty_init(&p->t);
				p->t.out = pty_out;
				p->t.priv = p;
				f->pty = i + 1;
				return 0;
			}
		return -ENOSPC;
	}
	p = slave_of(v);
	if (v->ino >= INO_SLAVE && v->ino < INO_SLAVE + NPTY) {
		if (!p || !p->master) return -EIO;
		p->slaves++;
		p->opened = 1;
		f->pty = -(int)(v->ino - INO_SLAVE) - 1;
	}
	return 0;
}

static Pty *pty_of(File *f) {
	int n = f ? f->pty : 0;
	if (n > 0) return &ptys[n - 1];
	if (n < 0) return &ptys[-n - 1];
	return NULL;
}

void pty_close(File *f) {
	Pty *p = pty_of(f);
	if (!p) return;
	if (f->pty > 0) {
		p->master = 0;
		p->t.hup = 1;
		if (p->t.pgrp) {
			signal_group(p->t.pgrp, SIGHUP, 0);
			signal_group(p->t.pgrp, SIGCONT, 0);
		}
		wakeup(&p->t.rwq);
		wakeup(&poll_wq);
	} else {
		p->slaves--;
		wakeup(&p->mwq);
		wakeup(&poll_wq);
	}
	f->pty = 0;
	if (!p->master && p->slaves <= 0) {
		int i;
		for (i = 0; i < NPROC; i++)
			if (procs[i].ctty == &p->t) procs[i].ctty = NULL;
		p->used = 0;
	}
}

int pty_read(Vnode *v, char *buf, unsigned n, File *f) {
	Pty *p = pty_of(f);
	unsigned i;
	if (v->ino >= INO_CTL && v->ino < INO_CTL + NPTY) return 0;
	if (!p) return -EIO;
	if (f->pty < 0) return tty_read(&p->t, buf, n, f);
	while (!p->ol) {
		if (p->opened && p->slaves <= 0) return 0;
		if (f->nonblock) return -EAGAIN;
		if (signal_pending()) return -EINTR;
		sleep_on(&p->mwq);
		if (signal_pending() && !p->ol) return -EINTR;
	}
	for (i = 0; i < n && p->ol; i++) {
		buf[i] = p->out[p->oh];
		p->oh = (p->oh + 1) % OBUF;
		p->ol--;
	}
	return (int)i;
}

int pty_write(Vnode *v, const char *buf, unsigned n, File *f) {
	Pty *p = pty_of(f);
	unsigned i;
	if (v->ino >= INO_CTL && v->ino < INO_CTL + NPTY) {
		char cmd[ERRMAX];
		int r = ctl_text(cmd, buf, n);
		if (r < 0) return r;
		if (!ptys[v->ino - INO_CTL].used) return -EIO;
		r = tty_ctl(&ptys[v->ino - INO_CTL].t, cmd);
		return r < 0 ? r : (int)n;
	}
	if (!p) return -EIO;
	if (f->pty < 0) {
		if (!p->master) return -EIO;
		return tty_write(&p->t, buf, n, f);
	}
	for (i = 0; i < n; i++) tty_input(&p->t, (unsigned char)buf[i]);
	return (int)n;
}

int pty_ioctl(Vnode *v, unsigned req, unsigned arg, File *f) {
	Pty *p = pty_of(f);
	(void)v;
	if (!p) return -ENOTTY;
	if (f->pty > 0 && req == TIOCGPTN) {
		unsigned n = (unsigned)(p - ptys);
		return copyout(arg, &n, 4) ? -EFAULT : 0;
	}
	return tty_ioctl(&p->t, req, arg, f);
}

int pty_poll(Vnode *v, File *f) {
	Pty *p = pty_of(f);
	(void)v;
	if (!p) return POLLNVAL;
	if (f->pty < 0) return tty_poll(&p->t, f) | (p->t.hup ? POLLIN | POLLHUP : 0);
	return (p->ol ? POLLIN : 0) | POLLOUT | (p->opened && p->slaves <= 0 ? POLLIN | POLLHUP : 0);
}
