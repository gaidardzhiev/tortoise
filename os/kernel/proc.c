/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define ARGMAX 8192

Proc procs[NPROC];
Proc *curproc;
char hostname[32] = "tortoise";
static unsigned sched_ksp;
static int nextpid = 1;
static int last_slot;

void check_orphans(int pgid);

Proc *find_pid(int pid) {
	int i;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_UNUSED && procs[i].pid == pid) return &procs[i];
	return NULL;
}

static int pid_used(int pid) {
	int i;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_UNUSED && (procs[i].pid == pid || procs[i].pgid == pid || procs[i].sid == pid)) return 1;
	return 0;
}

static int alloc_pid(void) {
	int pid;
	do {
		pid = nextpid++;
		if (nextpid >= 32768) nextpid = 1;
	} while (pid_used(pid));
	return pid;
}

static Proc *alloc_proc(unsigned ret) {
	int i;
	for (i = 0; i < NPROC; i++) {
		Proc *p = &procs[i];
		unsigned *ctx;
		if (p->state != P_UNUSED) continue;
		memset(p, 0, sizeof(Proc));
		p->kstack = low_alloc_n(KSTACK / PAGE);
		if (!p->kstack) return NULL;
		p->tf = (TrapFrame *)(p->kstack + KSTACK - sizeof(TrapFrame));
		ctx = (unsigned *)p->tf - 5;
		ctx[0] = ctx[1] = ctx[2] = ctx[3] = 0;
		ctx[4] = ret;
		p->ksp = (unsigned)ctx;
		p->pid = alloc_pid();
		p->state = P_EMBRYO;
		return p;
	}
	return NULL;
}

static void release_proc(Proc *p) {
	if (p->pgdir) free_pgdir(p->pgdir);
	free_regions(p->regions);
	low_free_n(p->kstack, KSTACK / PAGE);
	kfree(p->cmdline);
	kfree(p->environ);
	memset(p, 0, sizeof(Proc));
}

void proc_init(void) {
	memset(procs, 0, sizeof(procs));
}

void user_init(void) {
	Proc *p = alloc_proc((unsigned)first_start);
	File *f;
	Vnode *cv;
	if (!p) panic("cannot create init");
	p->pgdir = new_pgdir();
	p->cwd = vref(rootvn);
	p->cwdkey = vref(rootvn);
	strncpy(p->cwdpath, "/", 2);
	p->ns = ns_hold(ns_boot());
	p->egrp = egrp_hold(egrp_boot());
	p->ppid = 0;
	p->pgid = p->sid = p->pid;
	p->umask = 022;
	strncpy(p->name, "init", 31);
	if (vfs_resolve("/dev/console", rootvn, 1, &cv)) panic("no /dev/console");
	f = open_vnode(cv, O_RDWR);
	file_set_path(f, "/dev/console");
	p->files[0] = f;
	p->files[1] = file_dup(f);
	p->files[2] = file_dup(f);
	p->state = P_RUNNABLE;
}

void init_entry(void) {
	if (do_exec("/bin/init", 0) < 0) panic("cannot exec /bin/init");
}

void reap_autoreap(void) {
	int i;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state == P_ZOMBIE && procs[i].autoreap) release_proc(&procs[i]);
}

void scheduler(void) {
	for (;;) {
		int i, best = 100, pick = -1;
		reap_autoreap();
		for (i = 0; i < NPROC; i++)
			if (procs[i].state == P_RUNNABLE && procs[i].nice < best) best = procs[i].nice;
		for (i = 1; i <= NPROC && best < 100; i++) {
			int k = (last_slot + i) % NPROC;
			if (procs[k].state == P_RUNNABLE && procs[k].nice == best) {
				pick = k;
				break;
			}
		}
		if (pick >= 0) {
			Proc *p = &procs[pick];
			last_slot = pick;
			curproc = p;
			p->state = P_RUNNING;
			if (p->slice <= 0) p->slice = 20 - p->nice / 2;
			set_ksp(p->kstack + KSTACK);
			set_ptbase(p->pgdir);
			swtch(&sched_ksp, p->ksp);
			set_ptbase(kernel_pgdir);
			curproc = NULL;
		} else {
			cpu_idle();
		}
	}
}

void sched(void) {
	if (curproc->in_sys) {
		curproc->sys_units += rd(TIMER_COUNT) - curproc->sys_mark;
		curproc->stime += curproc->sys_units / 10;
		curproc->sys_units %= 10;
	}
	swtch(&curproc->ksp, sched_ksp);
	curproc->sys_mark = rd(TIMER_COUNT);
}

void yield(void) {
	curproc->state = P_RUNNABLE;
	curproc->slice = 0;
	sched();
}

void sleep_on(WaitQueue *wq) {
	if (signal_pending()) return;
	curproc->chan = wq;
	curproc->state = P_SLEEPING;
	wq->waiters++;
	sched();
	if (wq->waiters > 0 && curproc->chan == wq) wq->waiters--;
	curproc->chan = NULL;
}

static void wake(Proc *p) {
	WaitQueue *wq = p->chan;
	if (wq && wq->waiters > 0) wq->waiters--;
	p->chan = NULL;
	p->state = P_RUNNABLE;
}

void wakeup(WaitQueue *wq) {
	int i;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state == P_SLEEPING && procs[i].chan == wq) wake(&procs[i]);
}

void wakeup_one(WaitQueue *wq) {
	int i;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state == P_SLEEPING && procs[i].chan == wq) {
			wake(&procs[i]);
			return;
		}
}

static char *dupmem(const char *s, unsigned n) {
	char *d;
	if (!s || !n) return NULL;
	d = kmalloc(n);
	if (d) memcpy(d, s, n);
	return d;
}

static void set_root_cwd(Proc *p) {
	if (p->cwd) vput(p->cwd);
	if (p->cwdkey) vput(p->cwdkey);
	p->cwd = vref(rootvn);
	p->cwdkey = vref(rootvn);
	strncpy(p->cwdpath, "/", 2);
}

static int rfork_self(int flags) {
	int i;
	if (flags & RFCFDG)
		for (i = 0; i < NOFILE; i++)
			if (curproc->files[i]) {
				file_close(curproc->files[i]);
				curproc->files[i] = NULL;
				curproc->fdflags[i] = 0;
			}
	if (flags & (RFENVG | RFCENVG)) {
		struct Egrp *g = (flags & RFCENVG) ? egrp_clean() : egrp_copy(curproc->egrp ? curproc->egrp : egrp_boot());
		if (!g) return -ENOMEM;
		egrp_put(curproc->egrp);
		curproc->egrp = g;
	}
	if (flags & (RFNAMEG | RFCNAMEG)) {
		struct Ns *n = (flags & RFCNAMEG) ? ns_clean() : ns_copy(ns_current());
		if (!n) return -ENOMEM;
		ns_put(curproc->ns);
		curproc->ns = n;
		if (flags & RFCNAMEG) set_root_cwd(curproc);
	}
	return 0;
}

int do_fork(void) {
	return do_rfork(RFPROC | RFFDG);
}

int do_rfork(int flags) {
	Proc *np;
	int i;
	Region *rs;
	if (flags & ~(RFPROC | RFNAMEG | RFCNAMEG | RFFDG | RFCFDG | RFENVG | RFCENVG | RFNOWAIT | RFNOTEG)) return -EINVAL;
	if ((flags & RFNAMEG) && (flags & RFCNAMEG)) return -EINVAL;
	if ((flags & RFFDG) && (flags & RFCFDG)) return -EINVAL;
	if ((flags & RFENVG) && (flags & RFCENVG)) return -EINVAL;
	if (!(flags & RFPROC)) return rfork_self(flags);
	np = alloc_proc((unsigned)forkret);
	if (!np) return -ENOMEM;
	np->pgdir = new_pgdir();
	rs = copy_regions(curproc->regions);
	if (!np->pgdir || rs == (Region *)-1) {
		if (rs != (Region *)-1) free_regions(rs);
		release_proc(np);
		return -ENOMEM;
	}
	np->regions = rs;
	if (fork_space(curproc, np)) {
		release_proc(np);
		return -ENOMEM;
	}
	*np->tf = *curproc->tf;
	np->tf->r0 = 0;
	if (!(flags & RFCFDG)) {
		for (i = 0; i < NOFILE; i++)
			if (curproc->files[i]) np->files[i] = file_dup(curproc->files[i]);
		memcpy(np->fdflags, curproc->fdflags, NOFILE);
	}
	np->cwd = vref(curproc->cwd);
	np->cwdkey = vref(curproc->cwdkey ? curproc->cwdkey : curproc->cwd);
	memcpy(np->cwdpath, curproc->cwdpath, MAXPATH);
	if (flags & RFCNAMEG) {
		np->ns = ns_clean();
		set_root_cwd(np);
	} else if (flags & RFNAMEG) {
		np->ns = ns_copy(ns_current());
	} else {
		np->ns = ns_hold(ns_current());
	}
	if (flags & RFCENVG) np->egrp = egrp_clean();
	else if (flags & RFENVG) np->egrp = egrp_copy(curproc->egrp ? curproc->egrp : egrp_boot());
	else np->egrp = egrp_hold(curproc->egrp ? curproc->egrp : egrp_boot());
	np->heap_start = curproc->heap_start;
	np->brk = curproc->brk;
	np->stack_low = curproc->stack_low;
	np->ppid = (flags & RFNOWAIT) ? 1 : curproc->pid;
	np->pgid = curproc->pgid;
	np->sid = curproc->sid;
	np->uid = curproc->uid;
	np->euid = curproc->euid;
	np->suid = curproc->suid;
	np->gid = curproc->gid;
	np->egid = curproc->egid;
	np->sgid = curproc->sgid;
	memcpy(np->groups, curproc->groups, sizeof(np->groups));
	np->ngroups = curproc->ngroups;
	np->umask = curproc->umask;
	if (curproc->notify) {
		np->nq = kmalloc(sizeof(NoteQ));
		if (np->nq) {
			memset(np->nq, 0, sizeof(NoteQ));
			np->notify = curproc->notify;
		}
	}
	memcpy(np->sigact, curproc->sigact, sizeof(np->sigact));
	np->sigmask = curproc->sigmask;
	np->nice = curproc->nice;
	np->ctty = curproc->ctty;
	memcpy(np->name, curproc->name, 32);
	memcpy(np->exe, curproc->exe, sizeof(np->exe));
	np->cmdline = dupmem(curproc->cmdline, curproc->cmdlen);
	np->cmdlen = np->cmdline ? curproc->cmdlen : 0;
	np->environ = dupmem(curproc->environ, curproc->envlen);
	np->envlen = np->environ ? curproc->envlen : 0;
	np->state = P_RUNNABLE;
	return np->pid;
}

void do_exit(int status) {
	int i, mypgid = curproc->pgid;
	Proc *parent, *init = find_pid(1);
	if (curproc->pid == 1) {
		kprintf("panic: init exited with status %d\n", status);
		poweroff(99);
	}
	for (i = 0; i < NOFILE; i++)
		if (curproc->files[i]) {
			file_close(curproc->files[i]);
			curproc->files[i] = NULL;
		}
	if (curproc->cwd) {
		vput(curproc->cwd);
		curproc->cwd = NULL;
	}
	if (curproc->cwdkey) {
		vput(curproc->cwdkey);
		curproc->cwdkey = NULL;
	}
	if (curproc->ns) {
		ns_put(curproc->ns);
		curproc->ns = NULL;
	}
	if (curproc->egrp) {
		egrp_put(curproc->egrp);
		curproc->egrp = NULL;
	}
	note_free(curproc);
	if (curproc->alarm_id) {
		timer_cancel(curproc->alarm_id);
		curproc->alarm_id = 0;
	}
	for (i = 0; i < NPROC; i++) {
		Proc *c = &procs[i];
		if (c->state != P_UNUSED && c->ppid == curproc->pid) {
			c->ppid = 1;
			if (c->state == P_ZOMBIE) {
				if (init && init->sigact[SIGCHLD].handler == SIG_IGN) c->autoreap = 1;
				if (init) wakeup(&init->child_wq);
			}
			if (c->pgid != mypgid) check_orphans(c->pgid);
		}
	}
	if (curproc->pid == curproc->sid) tty_hangup_session(curproc->sid);
	curproc->status = (status & 0x10000) ? (status & 0x7F) : ((status & 0xFF) << 8);
	curproc->state = P_ZOMBIE;
	parent = find_pid(curproc->ppid);
	if (!parent || parent->sigact[SIGCHLD].handler == SIG_IGN) curproc->autoreap = 1;
	if (parent) {
		send_signal(parent, SIGCHLD);
		wakeup(&parent->child_wq);
	}
	check_orphans(mypgid);
	sched();
	panic("zombie returned");
}

static int child_match(Proc *p, int pid) {
	if (p->state == P_UNUSED || p->ppid != curproc->pid || p->autoreap) return 0;
	if (pid > 0) return p->pid == pid;
	if (pid == -1) return 1;
	if (pid == 0) return p->pgid == curproc->pgid;
	return p->pgid == -pid;
}

int do_waitpid(int pid, unsigned stp, int options) {
	for (;;) {
		int i, have = 0;
		for (i = 0; i < NPROC; i++) {
			Proc *p = &procs[i];
			int st, cpid;
			if (!child_match(p, pid)) continue;
			have = 1;
			cpid = p->pid;
			if (p->state == P_ZOMBIE) {
				st = p->status;
				if (stp && copyout(stp, &st, 4)) return -EFAULT;
				curproc->cutime += p->utime + p->cutime;
				curproc->cstime += p->stime + p->cstime;
				release_proc(p);
				return cpid;
			}
			if ((options & WUNTRACED) && p->state == P_STOPPED && p->stop_report) {
				st = (p->stopsig << 8) | 0x7F;
				if (stp && copyout(stp, &st, 4)) return -EFAULT;
				p->stop_report = 0;
				return cpid;
			}
			if ((options & WCONTINUED) && p->cont_report) {
				st = 0xFFFF;
				if (stp && copyout(stp, &st, 4)) return -EFAULT;
				p->cont_report = 0;
				return cpid;
			}
		}
		if (!have) return -ECHILD;
		if (options & WNOHANG) return 0;
		if (signal_pending()) return -EINTR;
		sleep_on(&curproc->child_wq);
	}
}

int do_wait(unsigned stp) {
	return do_waitpid(-1, stp, 0);
}

int do_kill(int pid) {
	Proc *p = find_pid(pid);
	if (!p || p->state == P_ZOMBIE || pid == 1) return -EINVAL;
	if (!may_signal(p)) return -EPERM;
	send_signal(p, SIGKILL);
	return 0;
}

static unsigned get32(const unsigned char *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static int load_seg(unsigned pd, Vnode *v, unsigned foff, unsigned va, unsigned n) {
	static char buf[512];
	while (n) {
		unsigned k = n > 512 ? 512 : n;
		if (vfs_read(v, buf, foff, k) != (int)k) return -ENOEXEC;
		if (copy_to_space(pd, va, buf, k)) return -ENOMEM;
		foff += k;
		va += k;
		n -= k;
	}
	return 0;
}

static int copy_strings(char *buf, unsigned *total, unsigned *offs, int *count, int max, unsigned uvec) {
	int n = 0, r;
	*count = 0;
	if (!uvec) return 0;
	for (;;) {
		unsigned a;
		if (copyin(&a, uvec + (unsigned)n * 4, 4)) return -EFAULT;
		if (!a) break;
		if (n >= max) return -E2BIG;
		r = copyinstr(buf + *total, a, ARGMAX - *total);
		if (r < 0) return r == -EINVAL ? -E2BIG : r;
		offs[n++] = *total;
		*total += (unsigned)r + 1;
	}
	*count = n;
	return 0;
}

static int append_str(char *buf, unsigned *total, const char *s, unsigned *off) {
	unsigned l = strlen(s) + 1;
	if (*total + l > ARGMAX) return -E2BIG;
	memcpy(buf + *total, s, l);
	*off = *total;
	*total += l;
	return 0;
}

static char *join(char *strs, unsigned *offs, int n, unsigned *len) {
	unsigned total = 0, k = 0;
	int i;
	char *d;
	*len = 0;
	if (n == 0) return NULL;
	for (i = 0; i < n; i++) total += strlen(strs + offs[i]) + 1;
	d = kmalloc(total);
	if (!d) return NULL;
	for (i = 0; i < n; i++) {
		unsigned l = strlen(strs + offs[i]) + 1;
		memcpy(d + k, strs + offs[i], l);
		k += l;
	}
	*len = total;
	return d;
}

static int exec_image(const char *path, Vnode *ino, char *strs, unsigned total, unsigned *aoff, int argc, unsigned *eoff, int envc) {
	unsigned char h[32];
	unsigned entry, ta, ts, da, dsz, bs, bend, pd, sp, i, heap, ptrs[132], textend;
	int r = -ENOEXEC;
	Stat2 st;
	Proc tmp;
	const char *base;
	vfs_getattr(ino, &st);
	if (vfs_read(ino, (char *)h, 0, 32) != 32 || memcmp(h, "TEX1", 4)) return -ENOEXEC;
	entry = get32(h + 4);
	ta = get32(h + 8);
	ts = get32(h + 12);
	da = get32(h + 16);
	dsz = get32(h + 20);
	bs = get32(h + 24);
	bend = ((da + dsz + 3) & ~3u) + bs;
	if (ta < USER_BASE || da < ta || ts > 0x10000000 || dsz > 0x10000000 || bs > 0x10000000 || bend > 0x40000000 || entry < ta || entry >= ta + ts) return -ENOEXEC;
	if (32 + ts + dsz > st.size) return -ENOEXEC;
	pd = new_pgdir();
	if (!pd) return -ENOMEM;
	memset(&tmp, 0, sizeof(tmp));
	tmp.pgdir = pd;
	heap = (bend + PAGE - 1) & ~(PAGE - 1);
	textend = (ta + ts + PAGE - 1) & ~(PAGE - 1);
	r = -ENOMEM;
	if (add_region(&tmp, ta & ~0xFFFu, textend, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, R_TEXT)) goto fail;
	if (bend > da && add_region(&tmp, (da & ~0xFFFu) < textend ? textend : (da & ~0xFFFu), heap, PROT_READ | PROT_WRITE, MAP_PRIVATE, R_DATA)) goto fail;
	if (add_region(&tmp, heap, heap, PROT_READ | PROT_WRITE, MAP_PRIVATE, R_HEAP)) goto fail;
	if (add_region(&tmp, USER_STACK_TOP - USER_STACK_MAX, USER_STACK_TOP, PROT_READ | PROT_WRITE, MAP_PRIVATE, R_STACK)) goto fail;
	if (alloc_user(pd, ta, ta + ts, PTE_W | PTE_U | PTE_X)) goto fail;
	if (bend > da && alloc_user(pd, da, bend, PTE_W | PTE_U)) goto fail;
	sp = USER_STACK_TOP - ((total + 3) & ~3u) - (unsigned)(argc + 1 + envc + 1) * 4;
	if (alloc_user(pd, sp & ~0xFFFu, USER_STACK_TOP, PTE_W | PTE_U)) goto fail;
	r = load_seg(pd, ino, 32, ta, ts);
	if (r) goto fail;
	r = load_seg(pd, ino, 32 + ts, da, dsz);
	if (r) goto fail;
	r = -ENOMEM;
	{
		unsigned strbase = USER_STACK_TOP - ((total + 3) & ~3u);
		if (copy_to_space(pd, strbase, strs, total)) goto fail;
		for (i = 0; i < (unsigned)argc; i++) ptrs[i] = strbase + aoff[i];
		ptrs[argc] = 0;
		for (i = 0; i < (unsigned)envc; i++) ptrs[argc + 1 + i] = strbase + eoff[i];
		ptrs[argc + 1 + envc] = 0;
	}
	if (copy_to_space(pd, sp, ptrs, (unsigned)(argc + 1 + envc + 1) * 4)) goto fail;
	{
		unsigned old = curproc->pgdir;
		Region *oldr = curproc->regions;
		curproc->pgdir = pd;
		curproc->regions = tmp.regions;
		set_ptbase(pd);
		if (old) free_pgdir(old);
		free_regions(oldr);
	}
	if ((st.mode & 06000) && ns_current() != ns_boot()) ns_enter_boot();
	note_free(curproc);
	if (curproc->hang) {
		curproc->hang = 0;
		send_signal(curproc, SIGSTOP);
	}
	if (st.mode & 04000) curproc->euid = st.uid;
	if (st.mode & 02000) curproc->egid = st.gid;
	curproc->suid = curproc->euid;
	curproc->sgid = curproc->egid;
	curproc->heap_start = curproc->brk = heap;
	curproc->stack_low = USER_STACK_TOP - USER_STACK_MAX;
	base = path;
	for (i = 0; path[i]; i++)
		if (path[i] == '/') base = path + i + 1;
	strncpy(curproc->name, base, 31);
	kfree(curproc->cmdline);
	kfree(curproc->environ);
	curproc->cmdline = join(strs, aoff, argc, &curproc->cmdlen);
	curproc->environ = join(strs, eoff, envc, &curproc->envlen);
	for (i = 0; i < NOFILE; i++)
		if (curproc->files[i] && (curproc->fdflags[i] & FD_CLOEXEC)) {
			file_close(curproc->files[i]);
			curproc->files[i] = NULL;
			curproc->fdflags[i] = 0;
		}
	for (i = 1; i < 32; i++)
		if (curproc->sigact[i].handler != SIG_IGN) {
			curproc->sigact[i].handler = SIG_DFL;
			curproc->sigact[i].flags = 0;
			curproc->sigact[i].mask = 0;
		}
	memset(curproc->tf, 0, sizeof(TrapFrame));
	curproc->tf->pc = entry;
	curproc->tf->usp = sp;
	curproc->tf->r1 = (unsigned)argc;
	curproc->tf->r2 = sp;
	curproc->tf->r3 = sp + (unsigned)(argc + 1) * 4;
	curproc->tf->flags = 0x30;
	return 0;
fail:
	free_pgdir(pd);
	free_regions(tmp.regions);
	return r;
}

static void abs_path(const char *path, char *out) {
	if (path[0] == '/') {
		strncpy(out, path, 127);
		out[127] = 0;
		return;
	}
	if (sys_getcwd_k(out, 120) < 0) out[0] = 0;
	if (strcmp(out, "/")) strncpy(out + strlen(out), "/", 2);
	strncpy(out + strlen(out), path, 127 - strlen(out));
	out[127] = 0;
}

static int open_exec(const char *path, Vnode **out) {
	Vnode *v;
	int r = vfs_resolve(path, curproc && curproc->cwd ? curproc->cwd : rootvn, 1, &v);
	if (r) return r;
	if (v->type == VT_DIR) {
		vput(v);
		return -EISDIR;
	}
	if (v->type != VT_REG || vfs_access(v, 1)) {
		vput(v);
		return -EACCES;
	}
	*out = v;
	return 0;
}

int do_execve(const char *path, unsigned uargv, unsigned uenvp) {
	char *strs = (char *)low_alloc_n(ARGMAX / PAGE);
	unsigned total = 0, aoff[68], eoff[66];
	int argc = 0, envc = 0, r;
	Vnode *v = NULL;
	char line[130], interp[128], spath[128];
	if (!strs) return -ENOMEM;
	r = copy_strings(strs, &total, aoff, &argc, 62, uargv);
	if (r == 0) r = copy_strings(strs, &total, eoff, &envc, 64, uenvp);
	if (r) goto out;
	r = open_exec(path, &v);
	if (r) {
		v = NULL;
		goto out;
	}
	strncpy(spath, path, 127);
	spath[127] = 0;
	{
		int n = vfs_read(v, line, 0, 128), i, k;
		if (n >= 2 && line[0] == '#' && line[1] == '!') {
			char *arg = NULL;
			unsigned noff[68];
			int nc = 0;
			line[n] = 0;
			for (i = 2; line[i] == ' ' || line[i] == '\t'; i++) ;
			for (k = 0; line[i] && line[i] != ' ' && line[i] != '\t' && line[i] != '\n' && k < 127; i++) interp[k++] = line[i];
			interp[k] = 0;
			while (line[i] == ' ' || line[i] == '\t') i++;
			if (line[i] && line[i] != '\n') {
				arg = line + i;
				while (line[i] && line[i] != '\n') i++;
				while (i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t')) i--;
				line[i] = 0;
			}
			if (!interp[0]) {
				r = -ENOEXEC;
				goto out;
			}
			if (append_str(strs, &total, interp, &noff[nc++]) || (arg && append_str(strs, &total, arg, &noff[nc++])) || append_str(strs, &total, spath, &noff[nc++])) {
				r = -E2BIG;
				goto out;
			}
			for (k = 1; k < argc && nc < 64; k++) noff[nc++] = aoff[k];
			memcpy(aoff, noff, (unsigned)nc * 4);
			argc = nc;
			vput(v);
			v = NULL;
			r = open_exec(interp, &v);
			if (r) {
				v = NULL;
				goto out;
			}
			strncpy(spath, interp, 127);
		}
	}
	r = exec_image(spath, v, strs, total, aoff, argc, eoff, envc);
	if (r == 0) abs_path(spath, curproc->exe);
out:
	if (v) vput(v);
	low_free_n((unsigned)strs, ARGMAX / PAGE);
	return r;
}

int do_exec(const char *path, unsigned uargv) {
	return do_execve(path, uargv, 0);
}

int do_sbrk(int incr) {
	unsigned old = curproc->brk, nb = old + (unsigned)incr;
	Region *h = NULL, *r;
	for (r = curproc->regions; r; r = r->next)
		if (r->kind == R_HEAP) h = r;
	if (!h) return -ENOMEM;
	if ((incr > 0 && nb < old) || (incr < 0 && nb > old) || nb < curproc->heap_start || nb > MMAP_TOP) return -ENOMEM;
	for (r = curproc->regions; r; r = r->next)
		if (r != h && r->start < ((nb + PAGE - 1) & ~(PAGE - 1)) && r->end > h->start && r->start >= h->start) return -ENOMEM;
	if (incr < 0) free_user(curproc->pgdir, (nb + PAGE - 1) & ~(PAGE - 1), (old + PAGE - 1) & ~(PAGE - 1));
	curproc->brk = nb;
	h->end = (nb + PAGE - 1) & ~(PAGE - 1);
	return (int)old;
}

int grow_stack(unsigned va) {
	return fault_in(curproc, va, 1) ? -EFAULT : 0;
}

static void sleep_timer(void *arg) {
	Proc *p = arg;
	p->timer_fired = 1;
	if (p->state == P_SLEEPING && p->chan == &p->sleep_wq) wake(p);
}

int do_sleep(unsigned n) {
	int id;
	if (n == 0) return 0;
	curproc->timer_fired = 0;
	id = timer_add(n, sleep_timer, curproc);
	if (!id) return -ENOMEM;
	while (!curproc->timer_fired) {
		if (signal_pending()) {
			timer_cancel(id);
			return -EINTR;
		}
		sleep_on(&curproc->sleep_wq);
	}
	return 0;
}

int do_procinfo(int idx, unsigned up) {
	int i, k = 0;
	for (i = 0; i < NPROC; i++) {
		Proc *p = &procs[i];
		if (p->state == P_UNUSED) continue;
		if (k++ == idx) {
			int info[11];
			info[0] = p->pid;
			info[1] = p->ppid;
			info[2] = p->state == P_STOPPED ? P_SLEEPING : p->state;
			memcpy(&info[3], p->name, 32);
			return copyout(up, info, 44) ? -EFAULT : 1;
		}
	}
	return 0;
}

int sys_setpgid(int pid, int pgid) {
	Proc *p = pid ? find_pid(pid) : curproc;
	int i;
	if (pgid < 0) return -EINVAL;
	if (!p || (p != curproc && p->ppid != curproc->pid)) return -ESRCH;
	if (p->sid != curproc->sid || p->pid == p->sid) return -EPERM;
	if (!pgid) pgid = p->pid;
	if (pgid != p->pid) {
		int ok = 0;
		for (i = 0; i < NPROC; i++)
			if (procs[i].state != P_UNUSED && procs[i].pgid == pgid && procs[i].sid == curproc->sid) ok = 1;
		if (!ok) return -EPERM;
	}
	p->pgid = pgid;
	return 0;
}

int sys_getpgid(int pid) {
	Proc *p = pid ? find_pid(pid) : curproc;
	return p ? p->pgid : -ESRCH;
}

int sys_setsid(void) {
	int i;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state != P_UNUSED && procs[i].pgid == curproc->pid) return -EPERM;
	curproc->sid = curproc->pgid = curproc->pid;
	curproc->ctty = NULL;
	return curproc->sid;
}

int sys_getsid(int pid) {
	Proc *p = pid ? find_pid(pid) : curproc;
	return p ? p->sid : -ESRCH;
}

#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif

const char *build_date(void) {
	return BUILD_DATE;
}

int last_pid(void) {
	return nextpid == 1 ? 32767 : nextpid - 1;
}

int sys_uname(unsigned u) {
	char b[160];
	memset(b, 0, sizeof(b));
	strncpy(b, "TORTOISE", 31);
	strncpy(b + 32, hostname, 31);
	strncpy(b + 64, "3.0", 31);
	strncpy(b + 96, BUILD_DATE, 31);
	strncpy(b + 128, "tortoise", 31);
	return copyout(u, b, sizeof(b)) ? -EFAULT : 0;
}

int sys_clock_gettime(int clk, unsigned uts) {
	unsigned ts[2], ms = uptime_ms();
	if (clk == 0) ts[0] = vfs_now();
	else if (clk == 1) ts[0] = ms / 1000;
	else return -EINVAL;
	ts[1] = (ms % 1000 / 10) * 10000000u;
	return copyout(uts, ts, 8) ? -EFAULT : 0;
}

int sys_nice(int inc) {
	int n = curproc->nice + inc;
	if (inc < 0 && curproc->euid) return -EPERM;
	if (n < -20) n = -20;
	if (n > 19) n = 19;
	curproc->nice = n;
	return n;
}

int sys_sethostname(unsigned name, unsigned len) {
	char b[32];
	if (curproc->euid) return -EPERM;
	if (len > 31) return -EINVAL;
	if (copyin(b, name, len)) return -EFAULT;
	b[len] = 0;
	memcpy(hostname, b, 32);
	return 0;
}

int sys_times(unsigned ut) {
	unsigned t[4];
	t[0] = curproc->utime;
	t[1] = curproc->stime;
	t[2] = curproc->cutime;
	t[3] = curproc->cstime;
	if (ut && copyout(ut, t, 16)) return -EFAULT;
	return (int)ticks;
}
