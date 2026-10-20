#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <sys/utsname.h>

static int fails;
static char zbuf[4096];

static void expect(const char *what, int got, int want) {
	if (got == want) printf("ok   %s\n", what);
	else {
		printf("FAIL %s: got %d want %d (errno %d)\n", what, got, want, errno);
		fails++;
	}
	fflush(stdout);
}

static unsigned now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 100 + ts.tv_nsec / 10000000;
}

static int spinner(int inc, int out, unsigned until) {
	int pid = fork();
	if (pid == 0) {
		unsigned n = 0;
		struct tms t;
		nice(inc);
		while (now() < until) n++;
		times(&t);
		write(out, &t.tms_utime, 4);
		_exit(0);
	}
	return pid;
}

int main(void) {
	struct utsname u;
	struct timespec a, b;
	struct tms t, t2;
	unsigned start, got[3];
	int i, p[2], st;
	char host[32];
	expect("uname", uname(&u), 0);
	printf("sysname %s, release %s, machine %s, nodename %s\n", u.sysname, u.release, u.machine, u.nodename);
	expect("version is a build date", strlen(u.version) == 10 && u.version[4] == '-', 1);
	expect("sethostname", sethostname("turtle", 6), 0);
	gethostname(host, sizeof(host));
	expect("gethostname", strcmp(host, "turtle"), 0);
	expect("sethostname too long", sethostname("abcdefghijklmnopqrstuvwxyz0123456789", 36) == -1 && errno == EINVAL, 1);
	clock_gettime(CLOCK_MONOTONIC, &a);
	sleep(25);
	clock_gettime(CLOCK_MONOTONIC, &b);
	{
		int d = (b.tv_sec * 100 + b.tv_nsec / 10000000) - (a.tv_sec * 100 + a.tv_nsec / 10000000);
		expect("monotonic clock advanced 25 to 27 ticks", d >= 25 && d <= 27, 1);
	}
	expect("nanoseconds in steps of 10 ms", b.tv_nsec % 10000000, 0);
	clock_gettime(CLOCK_REALTIME, &a);
	expect("realtime clock agrees with time()", (int)a.tv_sec - time() <= 1 && time() - (int)a.tv_sec <= 1, 1);
	expect("bad clock", clock_gettime(7, &a) == -1 && errno == EINVAL, 1);
	times(&t);
	start = now();
	while (now() < start + 30)
		for (i = 0; i < 2000; i++) st += i;
	times(&t2);
	expect("a user loop earns user ticks", t2.tms_utime - t.tms_utime >= 25, 1);
	p[0] = open("/dev/zero", O_RDONLY);
	start = now();
	while (now() < start + 30)
		for (i = 0; i < 20; i++) read(p[0], zbuf, sizeof(zbuf));
	close(p[0]);
	times(&t);
	expect("a loop of system calls earns system ticks", t.tms_stime - t2.tms_stime >= 15, 1);
	if (fork() == 0) {
		start = now();
		while (now() < start + 20)
			for (i = 0; i < 2000; i++) st += i;
		_exit(0);
	}
	wait(&st);
	times(&t);
	expect("children's ticks counted after wait", t.tms_cutime + t.tms_cstime >= 18, 1);
	pipe(p);
	start = now() + 5;
	spinner(0, p[1], start + 100);
	spinner(0, p[1], start + 100);
	spinner(10, p[1], start + 100);
	for (i = 0; i < 3; i++) wait(&st);
	for (i = 0; i < 3; i++) read(p[0], &got[i], 4);
	printf("ticks of the two nice 0 spinners and the nice 10 spinner, in order of exit:");
	for (i = 0; i < 3; i++) printf(" %s", got[i] > 30 ? "many" : got[i] < 5 ? "few" : "some");
	printf("\n");
	expect("nice returns the new value", nice(5), 5);
	expect("nice is clamped at 19", nice(100), 19);
	expect("root may lower nice", nice(-1), 18);
	printf("%s\n", fails ? "proctest: FAILED" : "proctest: all ok");
	return fails != 0;
}
