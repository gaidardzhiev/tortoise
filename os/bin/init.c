/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAXENT 16
#define MAXARG 16

typedef struct {
	char id[16];
	char action[16];
	char *argv[MAXARG + 1];
	char text[128];
	int pid;
} Entry;

static Entry ents[MAXENT];
static int nents;

static int start(Entry *e) {
	int pid = fork();
	if (pid < 0) {
		fprintf(stderr, "init: fork failed\n");
		return -1;
	}
	if (pid == 0) {
		execv(e->argv[0], e->argv);
		fprintf(stderr, "init: cannot run %s\n", e->argv[0]);
		_exit(127);
	}
	e->pid = pid;
	return pid;
}

static void parse(char *s) {
	Entry *e;
	char *a, *b;
	int n = 0;
	if (nents >= MAXENT || *s == '#' || *s == '\n' || !*s) return;
	a = strchr(s, ':');
	if (!a) return;
	b = strchr(a + 1, ':');
	if (!b) return;
	e = &ents[nents];
	*a = 0;
	*b = 0;
	strncpy(e->id, s, 15);
	strncpy(e->action, a + 1, 15);
	strncpy(e->text, b + 1, 127);
	s = e->text;
	while (*s && n < MAXARG) {
		while (*s == ' ' || *s == '\t' || *s == '\n') *s++ = 0;
		if (!*s) break;
		e->argv[n++] = s;
		while (*s && *s != ' ' && *s != '\t' && *s != '\n') s++;
	}
	e->argv[n] = NULL;
	if (n == 0) return;
	if (strcmp(e->action, "sysinit") && strcmp(e->action, "respawn") && strcmp(e->action, "once")) {
		fprintf(stderr, "init: unknown action %s\n", e->action);
		return;
	}
	nents++;
}

static void old_init(void) {
	Entry e;
	memset(&e, 0, sizeof(e));
	e.argv[0] = "/bin/sh";
	for (;;) {
		int pid = start(&e), st, w;
		if (pid < 0) return;
		do {
			w = wait(&st);
		} while (w >= 0 && w != pid);
	}
}

int main(void) {
	char buf[256];
	FILE *f = fopen("/etc/inittab", "r");
	int i, st, w;
	if (!f) {
		old_init();
		return 1;
	}
	while (fgets(buf, sizeof(buf), f)) parse(buf);
	fclose(f);
	for (i = 0; i < nents; i++)
		if (!strcmp(ents[i].action, "sysinit") && start(&ents[i]) > 0)
			while ((w = wait(&st)) >= 0 && w != ents[i].pid) ;
	for (i = 0; i < nents; i++)
		if (strcmp(ents[i].action, "sysinit")) start(&ents[i]);
	for (;;) {
		w = wait(&st);
		if (w < 0) {
			usleep(100 * 10000);
			continue;
		}
		for (i = 0; i < nents; i++)
			if (ents[i].pid == w) {
				ents[i].pid = 0;
				if (!strcmp(ents[i].action, "respawn")) start(&ents[i]);
			}
	}
}
