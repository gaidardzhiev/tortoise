/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

Tty contty;

typedef struct TtyWait {
	Tty *t;
	int fired;
} TtyWait;

void tty_init(Tty *t) {
	memset(&t->t, 0, sizeof(Termios));
	t->t.c_iflag = ICRNL;
	t->t.c_oflag = OPOST;
	t->t.c_lflag = ISIG | ICANON | ECHO | ECHOE;
	t->t.c_cc[VINTR] = 3;
	t->t.c_cc[VQUIT] = 28;
	t->t.c_cc[VERASE] = 127;
	t->t.c_cc[VKILL] = 21;
	t->t.c_cc[VEOF] = 4;
	t->t.c_cc[VMIN] = 1;
	t->t.c_cc[VTIME] = 0;
	t->t.c_cc[VSUSP] = 26;
	t->r = t->w = t->e = 0;
	t->pgrp = t->sid = 0;
	t->pollers = t->want = 0;
}

static void tty_out(Tty *t, int c) {
	if ((t->t.c_oflag & OPOST) && (t->t.c_oflag & ONLCR) && c == '\n') t->out(t, '\r');
	t->out(t, c);
}

static void echo(Tty *t, int c) {
	if (t->t.c_lflag & ECHO) tty_out(t, c);
}

static void erase_one(Tty *t) {
	if (t->e == t->w) return;
	t->e--;
	if ((t->t.c_lflag & ECHO) && (t->t.c_lflag & ECHOE)) {
		t->out(t, 8);
		t->out(t, ' ');
		t->out(t, 8);
	}
}

static void commit(Tty *t) {
	t->w = t->e;
	wakeup(&t->rwq);
	wakeup(&poll_wq);
}

static int accepting(Tty *t) {
	if (t->rwq.waiters == 0 && t->pollers == 0) return 0;
	if (t->hold) return 0;
	if (t->t.c_lflag & ICANON) return t->w == t->r;
	return t->w - t->r < (unsigned)(t->want > 0 ? t->want : 1);
}

void tty_input(Tty *t, int c) {
	Termios *m = &t->t;
	if (c == '\r' && (m->c_iflag & ICRNL)) c = '\n';
	if (m->c_lflag & ISIG) {
		int sig = 0;
		if (c && c == m->c_cc[VINTR]) sig = SIGINT;
		else if (c && c == m->c_cc[VQUIT]) sig = SIGQUIT;
		else if (c && c == m->c_cc[VSUSP]) sig = SIGTSTP;
		if (sig) {
			t->hold = 1;
			t->e = t->w;
			if (t->pgrp) signal_group(t->pgrp, sig, 0);
			wakeup(&t->rwq);
			return;
		}
	}
	if (m->c_lflag & ICANON) {
		if (c == m->c_cc[VERASE] || c == 8) {
			erase_one(t);
			return;
		}
		if (c == m->c_cc[VKILL]) {
			while (t->e != t->w) erase_one(t);
			return;
		}
		if (c == m->c_cc[VEOF]) {
			t->buf[t->e++ % TTYBUF] = (char)c;
			commit(t);
			return;
		}
		if (t->e - t->r >= TTYBUF - 1 && c != '\n') return;
		t->buf[t->e++ % TTYBUF] = (char)c;
		echo(t, c);
		if (c == '\n') commit(t);
		return;
	}
	if (t->e - t->r >= TTYBUF) return;
	t->buf[t->e++ % TTYBUF] = (char)c;
	echo(t, c);
	commit(t);
}

static void tty_timeout(void *a) {
	TtyWait *w = a;
	w->fired = 1;
	wakeup(&w->t->rwq);
}

static int orphan(int pgid) {
	int i;
	for (i = 0; i < NPROC; i++) {
		Proc *p = &procs[i], *par;
		if (p->state == P_UNUSED || p->state == P_ZOMBIE || p->pgid != pgid) continue;
		par = find_pid(p->ppid);
		if (par && par->pgid != pgid && par->sid == p->sid) return 0;
	}
	return 1;
}

static int background_check(Tty *t, int sig) {
	unsigned bit = 1u << sig;
	if (!curproc || curproc->ctty != t || !t->pgrp || curproc->pgid == t->pgrp) return 0;
	if (curproc->sigact[sig].handler == SIG_IGN || (curproc->sigmask & bit)) return sig == SIGTTIN ? -EIO : 0;
	if (orphan(curproc->pgid)) return -EIO;
	signal_group(curproc->pgid, sig, 0);
	return -EINTR;
}

static int wait_input(Tty *t, int ticks, int nonblock) {
	TtyWait w;
	int id = 0;
	if (nonblock) return -EAGAIN;
	w.t = t;
	w.fired = 0;
	if (ticks > 0) id = timer_add((unsigned)ticks, tty_timeout, &w);
	if (signal_pending()) {
		if (id) timer_cancel(id);
		return -EINTR;
	}
	if (t->enable) t->enable(t);
	sleep_on(&t->rwq);
	if (id && !w.fired) timer_cancel(id);
	if (signal_pending()) return -EINTR;
	return w.fired ? 1 : 0;
}

static void done_reading(Tty *t) {
	t->want = 0;
	if (t->disable && t->rwq.waiters == 0 && t->pollers == 0) t->disable(t);
}

static int canon_read(Tty *t, char *dst, unsigned n, int nonblock) {
	unsigned i = 0;
	int r;
	while (t->r == t->w) {
		if (t->hup) {
			done_reading(t);
			return 0;
		}
		r = wait_input(t, 0, nonblock);
		if (r < 0) {
			done_reading(t);
			return r;
		}
	}
	while (i < n && t->r != t->w) {
		char c = t->buf[t->r % TTYBUF];
		if ((unsigned char)c == t->t.c_cc[VEOF]) {
			t->r++;
			break;
		}
		t->r++;
		dst[i++] = c;
		if (c == '\n') break;
	}
	done_reading(t);
	return (int)i;
}

static int raw_read(Tty *t, char *dst, unsigned n, int nonblock) {
	unsigned min = t->t.c_cc[VMIN], i = 0;
	int time = t->t.c_cc[VTIME] * 10, r;
	if (min > n) min = n;
	t->want = (int)(min ? min : 1);
	if (min == 0 && time == 0) {
		if (t->pull && t->w == t->r) {
			t->want = (int)n;
			t->pull(t);
		}
	} else if (min == 0) {
		while (t->w == t->r) {
			r = wait_input(t, time, nonblock);
			if (r < 0) {
				done_reading(t);
				return r;
			}
			if (r == 1) break;
		}
	} else {
		while (t->w == t->r) {
			r = wait_input(t, 0, nonblock);
			if (r < 0) {
				done_reading(t);
				return r;
			}
		}
		while (t->w - t->r < min) {
			unsigned have = t->w - t->r;
			r = wait_input(t, time, nonblock);
			if (r < 0) {
				if (r == -EAGAIN) break;
				done_reading(t);
				return r;
			}
			if (r == 1 && t->w - t->r == have) break;
		}
	}
	while (i < n && t->r != t->w) dst[i++] = t->buf[t->r++ % TTYBUF];
	done_reading(t);
	return (int)i;
}

int tty_read(Tty *t, char *dst, unsigned n, File *f) {
	int r;
	if (!t) return -ENXIO;
	if (t->hup && t->r == t->w) return 0;
	r = background_check(t, SIGTTIN);
	if (r) return r;
	t->hold = 0;
	if (n == 0) return 0;
	if (t->t.c_lflag & ICANON) return canon_read(t, dst, n, f && f->nonblock);
	return raw_read(t, dst, n, f && f->nonblock);
}

int tty_write(Tty *t, const char *src, unsigned n, File *f) {
	unsigned i;
	(void)f;
	if (!t) return -ENXIO;
	for (i = 0; i < n; i++) tty_out(t, (unsigned char)src[i]);
	return (int)n;
}

int tty_poll(Tty *t, File *f) {
	(void)f;
	if (!t) return POLLNVAL;
	if (t->t.c_lflag & ICANON) return (t->r != t->w ? POLLIN : 0) | POLLOUT;
	return (t->w != t->r ? POLLIN : 0) | POLLOUT;
}

static int pgrp_in_session(int pgid, int sid) {
	int i;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_UNUSED && procs[i].state != P_ZOMBIE && procs[i].pgid == pgid && procs[i].sid == sid) return 1;
	return 0;
}

int tty_ioctl(Tty *t, unsigned req, unsigned arg, File *f) {
	int v, r;
	(void)f;
	if (!t) return -ENXIO;
	switch (req) {
	case TCGETS:
		return copyout(arg, &t->t, sizeof(Termios)) ? -EFAULT : 0;
	case TCSETS:
	case TCSETSW: {
		Termios m;
		r = background_check(t, SIGTTOU);
		if (r) return r;
		if (copyin(&m, arg, sizeof(m))) return -EFAULT;
		tty_setterm(t, &m);
		return 0;
	}
	case TIOCSCTTY:
		if (curproc->pid != curproc->sid) return -EPERM;
		if (curproc->ctty == t) return 0;
		if (curproc->ctty) return -EPERM;
		if (t->sid && t->sid != curproc->sid) {
			int i;
			if (arg != 1 || curproc->euid) return -EPERM;
			for (i = 0; i < NPROC; i++)
				if (procs[i].ctty == t) procs[i].ctty = NULL;
		}
		t->sid = curproc->sid;
		t->pgrp = curproc->pgid;
		curproc->ctty = t;
		return 0;
	case TIOCGPGRP:
		if (curproc->ctty != t) return -ENOTTY;
		return copyout(arg, &t->pgrp, 4) ? -EFAULT : 0;
	case TIOCSPGRP:
		if (curproc->ctty != t || t->sid != curproc->sid) return -ENOTTY;
		r = background_check(t, SIGTTOU);
		if (r) return r;
		if (copyin(&v, arg, 4)) return -EFAULT;
		if (v <= 0) return -EINVAL;
		if (!pgrp_in_session(v, curproc->sid)) return -EPERM;
		t->pgrp = v;
		return 0;
	case TIOCGWINSZ: {
		unsigned short ws[4];
		ws[0] = 25;
		ws[1] = 80;
		ws[2] = ws[3] = 0;
		return copyout(arg, ws, 8) ? -EFAULT : 0;
	}
	}
	return -EINVAL;
}

void tty_hangup_session(int sid) {
	int i;
	Tty *t = NULL;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_UNUSED && procs[i].sid == sid && procs[i].ctty) {
			t = procs[i].ctty;
			procs[i].ctty = NULL;
		}
	if (!t || t->sid != sid) return;
	if (t->pgrp) {
		signal_group(t->pgrp, SIGHUP, 0);
		signal_group(t->pgrp, SIGCONT, 0);
	}
	t->sid = 0;
	t->pgrp = 0;
}

static void con_out(Tty *t, int c) {
	(void)t;
	kputc(c);
}

static void con_enable(Tty *t) {
	(void)t;
	wr(UART_CTRL, 1);
}

static void con_disable(Tty *t) {
	(void)t;
	wr(UART_CTRL, 0);
}

static void con_pull(Tty *t) {
	while ((rd(UART_STATUS) & 1) && t->w - t->r < (unsigned)t->want) tty_input(t, (int)rd(UART_DATA));
}

void console_init(void) {
	tty_init(&contty);
	contty.out = con_out;
	contty.enable = con_enable;
	contty.disable = con_disable;
	contty.pull = con_pull;
	wr(UART_CTRL, 0);
}

void console_intr(void) {
	while (rd(UART_STATUS) & 1) {
		if (!accepting(&contty)) {
			wr(UART_CTRL, 0);
			return;
		}
		tty_input(&contty, (int)rd(UART_DATA));
	}
}

void console_poll_hold(int on) {
	if (on) {
		contty.hold = 0;
		contty.pollers++;
		wr(UART_CTRL, 1);
	} else if (contty.pollers > 0) {
		contty.pollers--;
	}
}

void tty_setterm(Tty *t, Termios *m) {
	if ((t->t.c_lflag & ICANON) && !(m->c_lflag & ICANON)) t->w = t->e;
	t->t = *m;
}

int tty_ctl(Tty *t, const char *cmd) {
	Termios m = t->t;
	const char *a;
	int r = background_check(t, SIGTTOU);
	if (r) return r;
	if (!strcmp(cmd, "rawon")) m.c_lflag &= ~(ICANON | ECHO);
	else if (!strcmp(cmd, "rawoff")) m.c_lflag |= ICANON | ECHO;
	else if (!strcmp(cmd, "echo on")) m.c_lflag |= ECHO;
	else if (!strcmp(cmd, "echo off")) m.c_lflag &= ~ECHO;
	else if (!strcmp(cmd, "isig on")) m.c_lflag |= ISIG;
	else if (!strcmp(cmd, "isig off")) m.c_lflag &= ~ISIG;
	else if (!strncmp(cmd, "vmin ", 5) || !strncmp(cmd, "vtime ", 6)) {
		unsigned v;
		a = cmd + (cmd[1] == 'm' ? 5 : 6);
		if (ctl_number(a, &v) || v > 255) return set_errstr(EINVAL, "bad number in control message", cmd);
		m.c_cc[cmd[1] == 'm' ? VMIN : VTIME] = (unsigned char)v;
	} else return set_errstr(EINVAL, "unknown control message", cmd);
	tty_setterm(t, &m);
	return 0;
}
