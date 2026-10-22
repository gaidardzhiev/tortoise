/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

static FILE *pwf, *grf;
static char pline[256], gline[512];
static struct passwd pw;
static struct group gr;
static char *members[33];

static char *cut(char **s) {
	char *start = *s, *p = *s;
	while (*p && *p != ':' && *p != '\n') p++;
	if (*p) {
		*p = 0;
		*s = p + 1;
	} else {
		*s = p;
	}
	return start;
}

void setpwent(void) {
	if (pwf) fclose(pwf);
	pwf = NULL;
}

void endpwent(void) {
	setpwent();
}

struct passwd *getpwent(void) {
	char *s;
	if (!pwf) pwf = fopen("/etc/passwd", "r");
	if (!pwf) return NULL;
	while (fgets(pline, sizeof(pline), pwf)) {
		if (pline[0] == '#' || pline[0] == '\n') continue;
		s = pline;
		pw.pw_name = cut(&s);
		pw.pw_passwd = cut(&s);
		pw.pw_uid = atoi(cut(&s));
		pw.pw_gid = atoi(cut(&s));
		pw.pw_gecos = cut(&s);
		pw.pw_dir = cut(&s);
		pw.pw_shell = cut(&s);
		return &pw;
	}
	return NULL;
}

struct passwd *getpwnam(const char *name) {
	struct passwd *p;
	setpwent();
	while ((p = getpwent()) != NULL)
		if (!strcmp(p->pw_name, name)) break;
	endpwent();
	return p;
}

struct passwd *getpwuid(int uid) {
	struct passwd *p;
	setpwent();
	while ((p = getpwent()) != NULL)
		if (p->pw_uid == uid) break;
	endpwent();
	return p;
}

void setgrent(void) {
	if (grf) fclose(grf);
	grf = NULL;
}

void endgrent(void) {
	setgrent();
}

struct group *getgrent(void) {
	char *s, *m;
	int n = 0;
	if (!grf) grf = fopen("/etc/group", "r");
	if (!grf) return NULL;
	while (fgets(gline, sizeof(gline), grf)) {
		if (gline[0] == '#' || gline[0] == '\n') continue;
		s = gline;
		gr.gr_name = cut(&s);
		gr.gr_passwd = cut(&s);
		gr.gr_gid = atoi(cut(&s));
		m = cut(&s);
		while (*m && n < 32) {
			members[n++] = m;
			while (*m && *m != ',') m++;
			if (*m) *m++ = 0;
		}
		members[n] = NULL;
		gr.gr_mem = members;
		return &gr;
	}
	return NULL;
}

struct group *getgrnam(const char *name) {
	struct group *g;
	setgrent();
	while ((g = getgrent()) != NULL)
		if (!strcmp(g->gr_name, name)) break;
	endgrent();
	return g;
}

struct group *getgrgid(int gid) {
	struct group *g;
	setgrent();
	while ((g = getgrent()) != NULL)
		if (g->gr_gid == gid) break;
	endgrent();
	return g;
}

int initgroups(const char *user, int gid) {
	int list[16], n = 0, i;
	struct group *g;
	list[n++] = gid;
	setgrent();
	while ((g = getgrent()) != NULL && n < 16)
		for (i = 0; g->gr_mem[i]; i++)
			if (!strcmp(g->gr_mem[i], user) && g->gr_gid != gid) {
				list[n++] = g->gr_gid;
				break;
			}
	endgrent();
	return setgroups(n, list);
}

char *crypt_t(const char *password, const char *name) {
	static char out[16];
	unsigned h = 2166136261u;
	const char *s;
	for (s = name; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
	for (s = password; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
	snprintf(out, sizeof(out), "$t$%08x", h);
	return out;
}
