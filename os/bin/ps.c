#include <stdio.h>
#include <unistd.h>

int main(void) {
	static const char *states[] = {"unused", "embryo", "ready", "run", "sleep", "zombie"};
	struct procinfo p;
	int i;
	printf("  PID  PPID STATE  NAME\n");
	for (i = 0; procinfo(i, &p) > 0; i++)
		printf("%5d %5d %-6s %s\n", p.pid, p.ppid, p.state >= 0 && p.state < 6 ? states[p.state] : "?", p.name);
	return 0;
}
