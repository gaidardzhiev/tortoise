/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>

static int fails;
static int count[32];
static int order[8];
static int norder;
static char *fixpage;

static void expect(const char *what, int got, int want) {
	if (got == want) printf("ok   %s\n", what);
	else {
		printf("FAIL %s: got %d want %d (errno %d)\n", what, got, want, errno);
		fails++;
	}
	fflush(stdout);
}

static void on_sig(int sig) {
	count[sig]++;
	if (norder < 8) order[norder++] = sig;
}

static void on_usr1_raise_usr2(int sig) {
	order[norder++] = sig;
	raise(SIGUSR2);
	order[norder++] = 100 + sig;
}

static void catch(int sig, void (*h)(int), int flags, sigset_t mask) {
	struct sigaction a;
	a.sa_handler = h;
	a.sa_mask = mask;
	a.sa_flags = flags;
	sigaction(sig, &a, NULL);
}

static void on_segv(int sig) {
	count[sig]++;
	mmap(fixpage, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
}

static void on_fault_exit(int sig) {
	printf("child caught signal %d\n", sig);
	fflush(stdout);
	_exit(sig);
}

static void on_hup(int sig) {
	write(4, "hup\n", 4);
	_exit(0);
}

static int fault_child(int kind) {
	int pid = fork(), st = 0;
	if (pid == 0) {
		char *code;
		int zero = 0;
		catch(SIGFPE, on_fault_exit, 0, 0);
		catch(SIGILL, on_fault_exit, 0, 0);
		catch(SIGTRAP, on_fault_exit, 0, 0);
		code = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (kind == 0) printf("%d\n", 100 / zero);
		code[0] = kind == 1 ? 0xC0 : 0x7C;
		((void (*)(void))code)();
		_exit(99);
	}
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int main(void) {
	sigset_t set, old;
	int p[2], r, st, pid, pid2, g;
	char c;

	catch(SIGUSR1, on_sig, 0, 0);
	raise(SIGUSR1);
	expect("caught signal runs the handler once", count[SIGUSR1], 1);
	expect("handler argument is the signal", order[0], SIGUSR1);
	signal(SIGUSR1, SIG_IGN);
	raise(SIGUSR1);
	expect("ignored signal does nothing", count[SIGUSR1], 1);
	sigpending(&set);
	expect("ignored signal is not pending", sigismember(&set, SIGUSR1), 0);

	catch(SIGUSR2, on_sig, 0, 0);
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, &old);
	raise(SIGUSR2);
	expect("blocked signal is not delivered", count[SIGUSR2], 0);
	sigpending(&set);
	expect("blocked signal is pending", sigismember(&set, SIGUSR2), 1);
	sigprocmask(SIG_SETMASK, &old, NULL);
	expect("unblocking delivers it", count[SIGUSR2], 1);
	sigpending(&set);
	expect("nothing pending afterwards", set, 0);
	sigemptyset(&set);
	sigaddset(&set, SIGKILL);
	sigaddset(&set, SIGSTOP);
	sigprocmask(SIG_SETMASK, &set, NULL);
	sigprocmask(SIG_SETMASK, NULL, &old);
	expect("SIGKILL and SIGSTOP cannot be blocked", old, 0);
	expect("SIGKILL cannot be caught", signal(SIGKILL, on_sig) == SIG_ERR && errno == EINVAL, 1);

	norder = 0;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	catch(SIGUSR1, on_usr1_raise_usr2, 0, set);
	raise(SIGUSR1);
	expect("sa_mask defers a signal until the handler returns", norder == 3 && order[0] == SIGUSR1 && order[1] == 100 + SIGUSR1 && order[2] == SIGUSR2, 1);

	pipe(p);
	catch(SIGALRM, on_sig, 0, 0);
	alarm(5);
	r = read(p[0], &c, 1);
	expect("blocked read interrupted: EINTR", r == -1 && errno == EINTR, 1);
	expect("alarm handler ran", count[SIGALRM], 1);
	catch(SIGALRM, on_sig, SA_RESTART, 0);
	pid = fork();
	if (pid == 0) {
		usleep(30 * 10000);
		write(p[1], "z", 1);
		_exit(0);
	}
	alarm(5);
	r = read(p[0], &c, 1);
	expect("SA_RESTART restarts the read", r == 1 && c == 'z', 1);
	expect("alarm handler ran again", count[SIGALRM], 2);
	waitpid(pid, NULL, 0);
	alarm(100);
	r = alarm(0);
	expect("alarm returns the remaining ticks", r > 90 && r <= 100, 1);
	expect("no previous alarm gives 0", alarm(0), 0);

	catch(SIGCHLD, on_sig, 0, 0);
	pid = fork();
	if (pid == 0) _exit(3);
	r = waitpid(pid, &st, 0);
	expect("waitpid returns the child", r, pid);
	expect("exit status 3", WIFEXITED(st) && WEXITSTATUS(st) == 3, 1);
	expect("SIGCHLD delivered", count[SIGCHLD], 1);
	signal(SIGCHLD, SIG_DFL);

	pid = fork();
	if (pid == 0) {
		pause();
		_exit(1);
	}
	expect("WNOHANG with a running child", waitpid(pid, &st, WNOHANG), 0);
	kill(pid, SIGSTOP);
	expect("WUNTRACED reports the stop", waitpid(pid, &st, WUNTRACED), pid);
	expect("WIFSTOPPED, WSTOPSIG", WIFSTOPPED(st) && WSTOPSIG(st) == SIGSTOP, 1);
	expect("stop reported only once", waitpid(pid, &st, WUNTRACED | WNOHANG), 0);
	kill(pid, SIGCONT);
	expect("WCONTINUED reports the continue", waitpid(pid, &st, WCONTINUED), pid);
	expect("WIFCONTINUED", WIFCONTINUED(st), 1);
	kill(pid, SIGTERM);
	waitpid(pid, &st, 0);
	expect("killed by SIGTERM", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM, 1);

	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		kill(getpid(), SIGTSTP);
		_exit(5);
	}
	setpgid(pid, pid);
	waitpid(pid, &st, WUNTRACED);
	expect("SIGTSTP stops a process", WIFSTOPPED(st) && WSTOPSIG(st) == SIGTSTP, 1);
	kill(pid, SIGCONT);
	waitpid(pid, &st, 0);
	expect("continues and exits", WIFEXITED(st) && WEXITSTATUS(st) == 5, 1);

	signal(SIGUSR1, SIG_DFL);
	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		pause();
		_exit(1);
	}
	setpgid(pid, pid);
	g = pid;
	pid2 = fork();
	if (pid2 == 0) {
		setpgid(0, g);
		pause();
		_exit(1);
	}
	setpgid(pid2, g);
	expect("getpgid", getpgid(pid2), g);
	expect("kill with signal 0 checks a group", kill(-g, 0), 0);
	expect("kill of a missing group", kill(-30000, 0) == -1 && errno == ESRCH, 1);
	pid = fork();
	if (pid == 0) _exit(9);
	expect("waitpid of another group skips our child", waitpid(-g, &st, WNOHANG), 0);
	expect("waitpid 0 takes our own group", waitpid(0, &st, 0) == pid && WEXITSTATUS(st) == 9, 1);
	kill(-g, SIGUSR1);
	r = waitpid(-g, &st, 0);
	expect("group member killed by SIGUSR1", WIFSIGNALED(st) && WTERMSIG(st) == SIGUSR1, 1);
	r = waitpid(-g, &st, 0);
	expect("second member too", WIFSIGNALED(st) && WTERMSIG(st) == SIGUSR1, 1);
	expect("no children left: ECHILD", waitpid(-1, &st, 0) == -1 && errno == ECHILD, 1);

	close(p[0]);
	close(p[1]);
	pipe(p);
	dup2(p[1], 4);
	pid = fork();
	if (pid == 0) {
		setsid();
		if (fork() == 0) {
			signal(SIGHUP, on_hup);
			kill(getpid(), SIGSTOP);
			_exit(1);
		}
		usleep(5 * 10000);
		_exit(0);
	}
	waitpid(pid, &st, 0);
	close(4);
	close(p[1]);
	r = read(p[0], order, 4);
	expect("orphaned stopped group gets SIGHUP and SIGCONT", r == 4 && memcmp(order, "hup\n", 4) == 0, 1);
	close(p[0]);
	expect("setsid fails for a group leader", (setpgid(0, 0), setsid()) == -1 && errno == EPERM, 1);

	catch(SIGSEGV, on_segv, 0, 0);
	fixpage = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	munmap(fixpage, 4096);
	fixpage[100] = 42;
	expect("SIGSEGV handler maps the page and the store is retried", count[SIGSEGV] == 1 && fixpage[100] == 42, 1);
	expect("divide by zero gives SIGFPE", fault_child(0), SIGFPE);
	expect("invalid opcode gives SIGILL", fault_child(1), SIGILL);
	expect("BRK gives SIGTRAP", fault_child(2), SIGTRAP);
	pipe(p);
	close(p[0]);
	pid = fork();
	if (pid == 0) {
		write(p[1], "x", 1);
		_exit(0);
	}
	waitpid(pid, &st, 0);
	expect("write to a pipe without readers: SIGPIPE", WIFSIGNALED(st) && WTERMSIG(st) == SIGPIPE, 1);
	signal(SIGPIPE, SIG_IGN);
	expect("with SIGPIPE ignored: EPIPE", write(p[1], "x", 1) == -1 && errno == EPIPE, 1);
	close(p[1]);
	printf("%s\n", fails ? "sigtest: FAILED" : "sigtest: all ok");
	return fails != 0;
}
