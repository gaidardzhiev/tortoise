#include "kernel.h"

unsigned ticks;

static const char *vec_name(unsigned v) {
	switch (v) {
	case 0: return "divide error";
	case 3: return "breakpoint";
	case 4: return "invalid opcode";
	case 5: return "bus error";
	case 6: return "double fault";
	case 13: return "protection fault";
	case 14: return "page fault";
	}
	return "exception";
}

static int higher_ready(int nice) {
	int i;
	for (i = 0; i < NPROC; i++)
		if (procs[i].state == P_RUNNABLE && procs[i].nice < nice) return 1;
	return 0;
}

static int syscall(TrapFrame *tf) {
	switch (tf->r0) {
	case 23: return do_waitpid((int)tf->r1, tf->r2, (int)tf->r3);
	case 24: return curproc->ppid;
	case 44: return sys_kill2((int)tf->r1, (int)tf->r2);
	case 45: return sys_sigaction((int)tf->r1, tf->r2, tf->r3);
	case 46: return sys_sigprocmask((int)tf->r1, tf->r2, tf->r3);
	case 47: return sys_sigreturn(tf);
	case 48: return sys_sigpending(tf->r1);
	case 49: return sys_pause();
	case 50: return sys_alarm(tf->r1);
	case 51: return sys_setpgid((int)tf->r1, (int)tf->r2);
	case 52: return sys_getpgid((int)tf->r1);
	case 53: return sys_setsid();
	case 54: return sys_getsid((int)tf->r1);
	case 62: {
		char path[128];
		int r = copyinstr(path, tf->r1, sizeof(path));
		if (r < 0) return r;
		return do_execve(path, tf->r2, tf->r3);
	}
	case 66: return sys_uname(tf->r1);
	case 67: return sys_clock_gettime((int)tf->r1, tf->r2);
	case 68: return sys_nice((int)tf->r1);
	case 69: return sys_sethostname(tf->r1, tf->r2);
	case 87: return sys_times(tf->r1);
	case 1: do_exit((int)tf->r1); return 0;
	case 2: return do_fork();
	case 3: return sys_read((int)tf->r1, tf->r2, tf->r3);
	case 4: return sys_write((int)tf->r1, tf->r2, tf->r3);
	case 5: return sys_open(tf->r1, (int)tf->r2);
	case 6: return sys_close((int)tf->r1);
	case 7: return do_wait(tf->r1);
	case 8: {
		char path[128];
		int r = copyinstr(path, tf->r1, sizeof(path));
		if (r < 0) return r;
		return do_exec(path, tf->r2);
	}
	case 9: return do_sbrk((int)tf->r1);
	case 10: return curproc->pid;
	case 11: return sys_unlink(tf->r1);
	case 12: return sys_mkdir(tf->r1);
	case 13: return sys_chdir(tf->r1);
	case 14: return sys_readdir((int)tf->r1, tf->r2);
	case 15: return sys_dup((int)tf->r1);
	case 16: return sys_pipe(tf->r1);
	case 17: return do_kill((int)tf->r1);
	case 18: return do_sleep(tf->r1);
	case 19: return (int)rd(RTC_SECONDS);
	case 20: poweroff((int)(tf->r1 & 0xFF)); return 0;
	case 21: return sys_stat(tf->r1, tf->r2);
	case 22: return do_procinfo((int)tf->r1, tf->r2);
	case 25: return sys_lseek((int)tf->r1, (int)tf->r2, (int)tf->r3);
	case 26: return sys_fstat((int)tf->r1, tf->r2);
	case 27: return sys_dup2((int)tf->r1, (int)tf->r2);
	case 28: return sys_rename(tf->r1, tf->r2);
	case 29: return sys_rmdir(tf->r1);
	case 30: return sys_getcwd(tf->r1, tf->r2);
	case 31: return sys_link(tf->r1, tf->r2);
	case 32: return sys_symlink(tf->r1, tf->r2);
	case 33: return sys_readlink(tf->r1, tf->r2, tf->r3);
	case 34: return sys_chmod(tf->r1, tf->r2);
	case 92: return sys_fchmod((int)tf->r1, tf->r2);
	case 55: return sys_fcntl((int)tf->r1, (int)tf->r2, tf->r3);
	case 56: return sys_ioctl((int)tf->r1, tf->r2, tf->r3);
	case 57: return sys_poll(tf->r1, tf->r2, (int)tf->r3);
	case 60: return sys_mount(tf->r1, tf->r2, tf->r3);
	case 61: return sys_umount(tf->r1);
	case 63: return sys_truncate(tf->r1, tf->r2);
	case 64: return sys_ftruncate((int)tf->r1, tf->r2);
	case 65: return sys_utime(tf->r1, tf->r2);
	case 84: return sys_stat2(tf->r1, tf->r2, 1);
	case 85: return sys_stat2(tf->r1, tf->r2, 0);
	case 86: return 0;
	case 58: return do_mmap(tf->r1, tf->r2, tf->r3, tf->r4, (int)tf->r5);
	case 59: return do_munmap(tf->r1, tf->r2);
	}
	return -EINVAL;
}

static void kernel_fault(TrapFrame *tf, unsigned vec) {
	kprintf("panic: kernel %s (vector %d) at PC=0x%08x FADDR=0x%08x CAUSE=0x%08x\n", vec_name(vec), vec, tf->pc, get_cr(2), get_cr(7));
	kprintf("R0=%08x R1=%08x R2=%08x R3=%08x\n", tf->r0, tf->r1, tf->r2, tf->r3);
	kprintf("R4=%08x R5=%08x R6=%08x R7=%08x FLAGS=%08x\n", tf->r4, tf->r5, tf->r6, tf->r7, tf->flags);
	poweroff(99);
}

void trap(TrapFrame *tf) {
	unsigned cause = get_cr(7), vec = cause & 0xFF;
	int user = (tf->flags & 0x20) != 0;
	if (vec >= 32 && vec < 40) {
		unsigned line = vec - 32;
		wr(PIC_ACK, 1u << line);
		if (line == 0) {
			ticks++;
			if (curproc) {
				if (user) curproc->utime++;
				else curproc->stime++;
				curproc->slice--;
			}
			timer_tick();
			if (user && curproc && (curproc->slice <= 0 || higher_ready(curproc->nice))) yield();
		} else if (line == 1) {
			console_intr();
		}
	} else if (vec == 48 && user) {
		int nr = (int)tf->r0, r;
		curproc->sys_nr = nr;
		curproc->eintr = 0;
		curproc->in_sys = 1;
		curproc->sys_mark = rd(TIMER_COUNT);
		r = syscall(tf);
		curproc->sys_units += rd(TIMER_COUNT) - curproc->sys_mark;
		curproc->stime += curproc->sys_units / 10;
		curproc->sys_units %= 10;
		curproc->in_sys = 0;
		if (nr == 47) {
			deliver_signals(tf);
			return;
		}
		tf->r0 = (unsigned)r;
		if (r == -EINTR) curproc->eintr = 1;
	} else {
		unsigned faddr = get_cr(2);
		if (!user || !curproc) kernel_fault(tf, vec);
		if (vec == 14 && fault_in(curproc, faddr, (cause >> 8) & 2) == 0) return;
		{
			int sig = fault_signal(vec);
			unsigned h = curproc->sigact[sig].handler;
			if (h == SIG_DFL || h == SIG_IGN || (curproc->sigmask & (1u << sig))) {
				kprintf("pid %d: fault %d at 0x%08x\n", curproc->pid, vec, tf->pc);
				exit_signal(sig);
			}
			send_signal(curproc, sig);
		}
	}
	if (user && curproc) deliver_signals(tf);
}
