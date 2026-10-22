/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

static const char *signames[32] = {
	"0", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM",
	"STKFLT", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "IO", "PWR", "SYS"
};

int note_signal(const char *n) {
	int i;
	if (!strcmp(n, "interrupt")) return SIGINT;
	if (!strcmp(n, "hangup")) return SIGHUP;
	if (!strcmp(n, "kill")) return SIGKILL;
	if (!strcmp(n, "alarm")) return SIGALRM;
	if (!strncmp(n, "sys: signal ", 12))
		for (i = 1; i < 32; i++)
			if (!strcmp(n + 12, signames[i])) return i;
	if (!strncmp(n, "sys: trap: fault", 16)) return SIGSEGV;
	return SIGTERM;
}

int notes_pending(Proc *p) {
	return p->notify && !p->innote && p->nnote > 0;
}

void post_note(Proc *p, const char *text) {
	if (!p || p->state == P_ZOMBIE || p->state == P_UNUSED) return;
	if (!p->notify) {
		send_signal(p, note_signal(text));
		return;
	}
	if (p->nnote >= NNOTE) return;
	strncpy(p->nq->notes[p->nnote], text, ERRMAX - 1);
	p->nq->notes[p->nnote][ERRMAX - 1] = 0;
	p->nnote++;
	if (p->state == P_SLEEPING) p->state = P_RUNNABLE;
}

int signal_to_note(Proc *p, int sig) {
	char n[32];
	if (!p->notify || sig == SIGKILL || sig == SIGSTOP) return 0;
	if (p->sigact[sig].handler != SIG_DFL) return 0;
	ksnprintf(n, sizeof(n), "sys: signal %s", signames[sig]);
	if (p->nnote >= NNOTE) return 1;
	strncpy(p->nq->notes[p->nnote++], n, ERRMAX - 1);
	return 1;
}

int deliver_note(TrapFrame *tf) {
	unsigned usp, w[3], ureg, msg;
	char text[ERRMAX];
	TrapFrame copy = *tf;
	int i, l;
	if (!notes_pending(curproc)) return 0;
	strncpy(text, curproc->nq->notes[0], ERRMAX);
	for (i = 1; i < curproc->nnote; i++) memcpy(curproc->nq->notes[i - 1], curproc->nq->notes[i], ERRMAX);
	curproc->nnote--;
	l = (int)strlen(text) + 1;
	usp = tf->usp - sizeof(TrapFrame);
	ureg = usp;
	if (copyout(usp, &copy, sizeof(TrapFrame))) return -1;
	usp -= (unsigned)((l + 3) & ~3);
	msg = usp;
	if (copyout(usp, text, (unsigned)l)) return -1;
	w[0] = 0;
	w[1] = ureg;
	w[2] = msg;
	usp -= 12;
	if (copyout(usp, w, 12)) return -1;
	strncpy(curproc->nq->cur, text, ERRMAX);
	curproc->noteureg = ureg;
	curproc->innote = 1;
	tf->usp = usp;
	tf->pc = curproc->notify;
	return 1;
}

int sys_notify(unsigned fn) {
	if (fn && !curproc->nq) {
		curproc->nq = kmalloc(sizeof(NoteQ));
		if (!curproc->nq) return -ENOMEM;
		memset(curproc->nq, 0, sizeof(NoteQ));
	}
	curproc->notify = fn;
	return 0;
}

void note_free(Proc *p) {
	kfree(p->nq);
	p->nq = NULL;
	p->notify = 0;
	p->nnote = 0;
	p->innote = 0;
}

int sys_noted(TrapFrame *tf, int how) {
	TrapFrame saved;
	int sig;
	if (!curproc->innote) return -EINVAL;
	if (how == NCONT) {
		if (copyin(&saved, curproc->noteureg, sizeof(TrapFrame))) {
			exit_signal(SIGSEGV);
			return 0;
		}
		saved.flags = (saved.flags & 0x0F) | 0x30;
		*tf = saved;
		curproc->innote = 0;
		curproc->eintr = 0;
		return (int)saved.r0;
	}
	if (how != NDFLT) return -EINVAL;
	curproc->innote = 0;
	sig = note_signal(curproc->nq->cur);
	if (copyin(&saved, curproc->noteureg, sizeof(TrapFrame)) == 0) {
		saved.flags = (saved.flags & 0x0F) | 0x30;
		*tf = saved;
	}
	note_default(sig);
	return (int)tf->r0;
}
