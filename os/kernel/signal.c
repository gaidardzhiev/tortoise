#include "kernel.h"

#define BIT(s) (1u << (s))
#define UNBLOCKABLE (BIT(SIGKILL) | BIT(SIGSTOP))
#define STOPSIGS (BIT(SIGSTOP) | BIT(SIGTSTP) | BIT(SIGTTIN) | BIT(SIGTTOU))

static int default_ignored(int sig) {
	return sig == SIGCHLD || sig == SIGCONT || sig == 23 || sig == 28;
}

static int ignored(Proc *p, int sig) {
	unsigned h = p->sigact[sig].handler;
	if (sig == SIGKILL || sig == SIGSTOP) return 0;
	if (h == SIG_IGN) return 1;
	return h == SIG_DFL && default_ignored(sig);
}

static int orphan_pgrp(int pgid);

void send_signal(Proc *p, int sig) {
	if (!p || sig <= 0 || sig >= 32 || p->state == P_ZOMBIE || p->state == P_UNUSED) return;
	if (sig == SIGCONT) {
		p->sigpend &= ~STOPSIGS;
		if (p->state == P_STOPPED) {
			Proc *par = find_pid(p->ppid);
			p->state = P_RUNNABLE;
			p->cont_report = 1;
			p->stop_report = 0;
			if (par) {
				send_signal(par, SIGCHLD);
				wakeup(&par->child_wq);
			}
		}
	}
	if (p->pid == 1 && p->sigact[sig].handler == SIG_DFL) return;
	if (BIT(sig) & STOPSIGS) p->sigpend &= ~BIT(SIGCONT);
	if (sig == SIGKILL && p->state == P_STOPPED) p->state = P_RUNNABLE;
	if (ignored(p, sig)) return;
	p->sigpend |= BIT(sig);
	if (p->state == P_SLEEPING && (!(p->sigmask & BIT(sig)) || (BIT(sig) & UNBLOCKABLE))) {
		p->state = P_RUNNABLE;
	}
}

int signal_pending(void) {
	if (!curproc) return 0;
	return (curproc->sigpend & (~curproc->sigmask | UNBLOCKABLE)) != 0;
}

void send_sigpipe(void) {
	send_signal(curproc, SIGPIPE);
}

static int may_signal(Proc *p) {
	if (curproc->euid == 0 || curproc->uid == 0) return 1;
	return curproc->uid == p->uid || curproc->uid == p->suid || curproc->euid == p->uid || curproc->euid == p->suid;
}

int signal_group(int pgid, int sig, int check) {
	int i, n = 0, denied = 0;
	for (i = 0; i < NPROC; i++) {
		Proc *p = &procs[i];
		if (p->state == P_UNUSED || p->state == P_ZOMBIE || p->pgid != pgid) continue;
		if (check && !may_signal(p)) {
			denied = 1;
			continue;
		}
		n++;
		if (sig) send_signal(p, sig);
	}
	if (n) return 0;
	return denied ? -EPERM : -ESRCH;
}

int sys_kill2(int pid, int sig) {
	int i, n = 0, denied = 0;
	Proc *p;
	if (sig < 0 || sig >= 32) return -EINVAL;
	if (pid > 0) {
		p = find_pid(pid);
		if (!p || p->state == P_ZOMBIE) return -ESRCH;
		if (!may_signal(p)) return -EPERM;
		if (sig) send_signal(p, sig);
		return 0;
	}
	if (pid == 0) return signal_group(curproc->pgid, sig, 1);
	if (pid < -1) return signal_group(-pid, sig, 1);
	for (i = 0; i < NPROC; i++) {
		p = &procs[i];
		if (p->state == P_UNUSED || p->state == P_ZOMBIE || p->pid == 1 || p == curproc) continue;
		if (!may_signal(p)) {
			denied = 1;
			continue;
		}
		n++;
		if (sig) send_signal(p, sig);
	}
	return n ? 0 : denied ? -EPERM : -ESRCH;
}

int sys_sigaction(int sig, unsigned act, unsigned old) {
	SigAct a;
	if (sig <= 0 || sig >= 32) return -EINVAL;
	if (old && copyout(old, &curproc->sigact[sig], sizeof(SigAct))) return -EFAULT;
	if (!act) return 0;
	if (sig == SIGKILL || sig == SIGSTOP) return -EINVAL;
	if (copyin(&a, act, sizeof(a))) return -EFAULT;
	a.mask &= ~UNBLOCKABLE;
	curproc->sigact[sig] = a;
	if (ignored(curproc, sig)) curproc->sigpend &= ~BIT(sig);
	return 0;
}

int sys_sigprocmask(int how, unsigned uset, unsigned old) {
	unsigned set;
	if (old && copyout(old, &curproc->sigmask, 4)) return -EFAULT;
	if (!uset) return 0;
	if (copyin(&set, uset, 4)) return -EFAULT;
	if (how == 0) curproc->sigmask |= set;
	else if (how == 1) curproc->sigmask &= ~set;
	else if (how == 2) curproc->sigmask = set;
	else return -EINVAL;
	curproc->sigmask &= ~UNBLOCKABLE;
	return 0;
}

int sys_sigpending(unsigned uset) {
	unsigned s = curproc->sigpend & curproc->sigmask;
	return copyout(uset, &s, 4) ? -EFAULT : 0;
}

int sys_pause(void) {
	static WaitQueue pause_wq;
	while (!signal_pending()) sleep_on(&pause_wq);
	return -EINTR;
}

static void alarm_fire(void *arg) {
	Proc *p = arg;
	p->alarm_id = 0;
	send_signal(p, SIGALRM);
}

int sys_alarm(unsigned n) {
	unsigned left = 0;
	if (curproc->alarm_id) {
		left = curproc->alarm_at - ticks;
		if ((int)left < 0) left = 0;
		timer_cancel(curproc->alarm_id);
		curproc->alarm_id = 0;
	}
	if (n) {
		curproc->alarm_id = timer_add(n, alarm_fire, curproc);
		curproc->alarm_at = ticks + n;
	}
	return (int)left;
}

int sys_sigreturn(TrapFrame *tf) {
	unsigned usp = tf->usp, mask;
	TrapFrame saved;
	if (copyin(&mask, usp + 4, 4) || copyin(&saved, usp + 8, sizeof(TrapFrame))) {
		exit_signal(SIGSEGV);
		return 0;
	}
	saved.flags = (saved.flags & 0x0F) | 0x30;
	*tf = saved;
	curproc->sigmask = mask & ~UNBLOCKABLE;
	curproc->eintr = 0;
	return (int)saved.r0;
}

static void stop_self(int sig) {
	Proc *par = find_pid(curproc->ppid);
	curproc->stopsig = sig;
	curproc->stop_report = 1;
	curproc->cont_report = 0;
	curproc->state = P_STOPPED;
	if (par) {
		send_signal(par, SIGCHLD);
		wakeup(&par->child_wq);
	}
	sched();
}

void exit_signal(int sig) {
	curproc->sigpend = 0;
	do_exit(0x10000 | sig);
}

static int push_frame(TrapFrame *tf, int sig, SigAct *a) {
	unsigned usp = tf->usp, w[3];
	TrapFrame copy = *tf;
	usp -= sizeof(TrapFrame);
	if (copyout(usp, &copy, sizeof(TrapFrame))) return -1;
	w[0] = a->restorer;
	w[1] = (unsigned)sig;
	w[2] = curproc->sigmask;
	usp -= 12;
	if (copyout(usp, w, 12)) return -1;
	tf->usp = usp;
	tf->pc = a->handler;
	curproc->sigmask |= a->mask;
	if (!(a->flags & SA_NODEFER)) curproc->sigmask |= BIT(sig);
	curproc->sigmask &= ~UNBLOCKABLE;
	if (a->flags & SA_RESETHAND) a->handler = SIG_DFL;
	return 0;
}

void deliver_signals(TrapFrame *tf) {
	for (;;) {
		unsigned ready = curproc->sigpend & (~curproc->sigmask | UNBLOCKABLE);
		int sig;
		SigAct *a;
		if (!ready) break;
		for (sig = 1; !(ready & BIT(sig)); sig++) ;
		curproc->sigpend &= ~BIT(sig);
		a = &curproc->sigact[sig];
		if (sig != SIGKILL && sig != SIGSTOP && a->handler == SIG_IGN) continue;
		if (sig == SIGKILL || sig == SIGSTOP || a->handler == SIG_DFL) {
			if (default_ignored(sig)) continue;
			if (BIT(sig) & STOPSIGS) {
				if (sig != SIGSTOP && orphan_pgrp(curproc->pgid)) continue;
				stop_self(sig);
				continue;
			}
			exit_signal(sig);
		}
		if (curproc->eintr && (a->flags & SA_RESTART) && curproc->sys_nr != 49 && curproc->sys_nr != 18 && curproc->sys_nr != 57) {
			tf->pc -= 1;
			tf->r0 = (unsigned)curproc->sys_nr;
		}
		curproc->eintr = 0;
		if (push_frame(tf, sig, a)) {
			curproc->sigact[SIGSEGV].handler = SIG_DFL;
			exit_signal(SIGSEGV);
		}
		return;
	}
	if (curproc->eintr) {
		tf->pc -= 1;
		tf->r0 = (unsigned)curproc->sys_nr;
		curproc->eintr = 0;
	}
}

int fault_signal(unsigned vec) {
	switch (vec) {
	case 0: return SIGFPE;
	case 3: return SIGTRAP;
	case 4: return SIGILL;
	case 5: return SIGBUS;
	}
	return SIGSEGV;
}

static int orphan_pgrp(int pgid) {
	int i, members = 0;
	for (i = 0; i < NPROC; i++) {
		Proc *p = &procs[i], *par;
		if (p->state == P_UNUSED || p->state == P_ZOMBIE || p->pgid != pgid) continue;
		members++;
		par = find_pid(p->ppid);
		if (par && par->state != P_ZOMBIE && par->pgid != pgid && par->sid == p->sid) return 0;
	}
	return members > 0;
}

void check_orphans(int pgid) {
	int i, stopped = 0;
	if (!orphan_pgrp(pgid)) return;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state == P_STOPPED && procs[i].pgid == pgid) stopped = 1;
	if (!stopped) return;
	signal_group(pgid, SIGHUP, 0);
	signal_group(pgid, SIGCONT, 0);
}
