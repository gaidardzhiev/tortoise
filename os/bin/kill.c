#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static const char *names[] = {
	"", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM",
	"", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU"
};

static int signum(const char *s) {
	int i;
	if (s[0] >= '0' && s[0] <= '9') return atoi(s);
	if (strncmp(s, "SIG", 3) == 0) s += 3;
	for (i = 1; i < 23; i++)
		if (names[i][0] && strcmp(s, names[i]) == 0) return i;
	return -1;
}

int main(int argc, char **argv) {
	int i = 1, rc = 0, sig = SIGTERM;
	if (argc > 1 && strcmp(argv[1], "-l") == 0) {
		for (i = 1; i < 23; i++)
			if (names[i][0]) printf("%d %s\n", i, names[i]);
		return 0;
	}
	if (argc > 1 && argv[1][0] == '-' && argv[1][1]) {
		sig = signum(argv[1] + 1);
		if (sig < 0 || sig >= 32) {
			printf("kill: bad signal %s\n", argv[1] + 1);
			return 1;
		}
		i = 2;
	}
	for (; i < argc; i++)
		if (kill(atoi(argv[i]), sig) < 0) {
			printf("kill: no process %s\n", argv[i]);
			rc = 1;
		}
	return rc;
}
