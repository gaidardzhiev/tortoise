/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "cc.h"
#include <unistd.h>
#include <sys/wait.h>

static char *cur_src;
static char *tooldir;
static char *libdir;
static char *defs[64];
static int ndefs;
static char *uincs[32];
static int nuincs;
static char *tmps[256];
static int ntmps;
static int keep;

void *xmalloc(int n) {
	void *p = malloc(n > 0 ? n : 1);
	if (!p) fatal("out of memory");
	memset(p, 0, n > 0 ? n : 1);
	return p;
}

char *xstrdup(const char *s) {
	char *d = xmalloc((int)strlen(s) + 1);
	strcpy(d, s);
	return d;
}

char *fmt(const char *f, ...) {
	char buf[4096];
	va_list ap;
	va_start(ap, f);
	vsnprintf(buf, sizeof(buf), f, ap);
	va_end(ap);
	return xstrdup(buf);
}

void error_at(Tok *t, const char *f, ...) {
	va_list ap;
	if (t && t->file) fprintf(stderr, "%s:%d: error: ", t->file, t->line);
	else fprintf(stderr, "%s: error: ", cur_src ? cur_src : "tcc0");
	va_start(ap, f);
	vfprintf(stderr, f, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

void fatal(const char *f, ...) {
	va_list ap;
	fprintf(stderr, "tcc0: ");
	va_start(ap, f);
	vfprintf(stderr, f, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

static int exists(const char *p) {
	FILE *f = fopen(p, "r");
	if (!f) return 0;
	fclose(f);
	return 1;
}

static int run(char **argv) {
	int pid, st = 0;
	pid = fork();
	if (pid < 0) fatal("cannot fork");
	if (pid == 0) {
		execv(argv[0], argv);
		fprintf(stderr, "tcc0: cannot run %s\n", argv[0]);
		exit(127);
	}
	if (wait(&st) < 0) fatal("wait failed");
	if (WIFEXITED(st)) return WEXITSTATUS(st);
	return 1;
}

static void cleanup(void) {
	int i;
	if (keep) return;
	for (i = 0; i < ntmps; i++) unlink(tmps[i]);
}

static char *tmpname(const char *base, const char *ext) {
	char *s = fmt("%s.%d.tmp%s", base, ntmps, ext);
	if (ntmps < 256) tmps[ntmps++] = s;
	return s;
}

static int compile(const char *src, const char *out) {
	int pid, st = 0, i;
	pid = fork();
	if (pid < 0) fatal("cannot fork");
	if (pid == 0) {
		Tok *t;
		cur_src = (char *)src;
		for (i = 0; i < nuincs; i++) incdirs[nincdirs++] = uincs[i];
		incdirs[nincdirs++] = fmt("%s/include", libdir);
		for (i = 0; i < ndefs; i++) pp_define(defs[i]);
		t = preprocess(src);
		parse_program(t);
		outf = fopen(out, "w");
		if (!outf) fatal("cannot create '%s'", out);
		gen_program();
		if (fclose(outf)) fatal("cannot write '%s'", out);
		exit(0);
	}
	if (wait(&st) < 0) fatal("wait failed");
	if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return 0;
	unlink(out);
	return 1;
}

static int assemble(const char *src, const char *out) {
	char *argv[6];
	argv[0] = fmt("%s/as", tooldir);
	argv[1] = "-c";
	argv[2] = (char *)src;
	argv[3] = "-o";
	argv[4] = (char *)out;
	argv[5] = NULL;
	return run(argv);
}

static int ends(const char *s, const char *e) {
	int a = (int)strlen(s), b = (int)strlen(e);
	return a >= b && strcmp(s + a - b, e) == 0;
}

static char *stem(const char *s) {
	char *d = xstrdup(s);
	char *dot = strrchr(d, '.');
	char *sl = strrchr(d, '/');
	if (dot && (!sl || dot > sl)) *dot = 0;
	return d;
}

static void usage(void) {
	fprintf(stderr, "usage: tcc0 [-S|-c] [-bare] [-I dir] [-D name[=val]] [-o out] file ...\n");
	exit(1);
}

int main(int argc, char **argv) {
	int mode = 0, bare = 0, i, nin = 0, rc = 0;
	char *out = NULL;
	char *in[256];
	char *objs[256];
	int nobjs = 0;
	char *sl;
	tooldir = xstrdup(argv[0]);
	sl = strrchr(tooldir, '/');
	if (sl) *sl = 0;
	else tooldir = exists("as") ? "." : "/bin";
	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (strcmp(a, "-S") == 0) mode = 1;
		else if (strcmp(a, "-c") == 0) mode = 2;
		else if (strcmp(a, "-bare") == 0) bare = 1;
		else if (strcmp(a, "-keep") == 0) keep = 1;
		else if (strcmp(a, "-o") == 0 && i + 1 < argc) out = argv[++i];
		else if (strncmp(a, "-I", 2) == 0) {
			char *d = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
			if (!d || nuincs >= 30) usage();
			uincs[nuincs++] = d;
		} else if (strncmp(a, "-D", 2) == 0) {
			char *d = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
			if (!d || ndefs >= 64) usage();
			defs[ndefs++] = d;
		} else if (strncmp(a, "-L", 2) == 0) {
			libdir = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
			if (!libdir) usage();
		} else if (a[0] == '-') usage();
		else {
			if (nin >= 256) fatal("too many input files");
			in[nin++] = a;
		}
	}
	if (nin == 0) usage();
	if (!libdir) {
		libdir = fmt("%s/lib", tooldir);
		if (!exists(fmt("%s/include/stdio.h", libdir))) libdir = fmt("%s/../lib", tooldir);
	}
	if (bare) defs[ndefs++] = "__BARE__=1";
	if ((mode == 1 || mode == 2) && out && nin > 1) fatal("-o with -S or -c needs a single input file");
	for (i = 0; i < nin; i++) {
		char *f = in[i];
		if (ends(f, ".c")) {
			char *s, *o;
			if (mode == 1) {
				s = out ? out : fmt("%s.s", stem(f));
				if (compile(f, s)) {
					rc = 1;
					break;
				}
				continue;
			}
			s = tmpname(stem(out && mode == 2 ? out : f), ".s");
			if (compile(f, s)) {
				rc = 1;
				break;
			}
			o = mode == 2 ? (out ? out : fmt("%s.o", stem(f))) : tmpname(stem(f), ".o");
			if (assemble(s, o)) {
				rc = 1;
				break;
			}
			objs[nobjs++] = o;
		} else if (ends(f, ".s") || ends(f, ".asm")) {
			char *o;
			if (mode == 1) fatal("-S given an assembly file");
			o = mode == 2 ? (out ? out : fmt("%s.o", stem(f))) : tmpname(stem(f), ".o");
			if (assemble(f, o)) {
				rc = 1;
				break;
			}
			objs[nobjs++] = o;
		} else if (ends(f, ".o")) {
			objs[nobjs++] = f;
		} else {
			fatal("unknown file type '%s'", f);
		}
	}
	if (rc == 0 && mode == 0) {
		static const char *libs[] = {"memops.o", "string.o", "stdlib.o", "stdio.o", "ctype.o", NULL};
		char *largv[300];
		int n = 0, k;
		char *v = bare ? "bare" : "os";
		largv[n++] = fmt("%s/tlink", tooldir);
		largv[n++] = "-T";
		largv[n++] = bare ? "0" : "0x00400000";
		largv[n++] = "-o";
		largv[n++] = out ? out : "a.out";
		largv[n++] = fmt("%s/%s/%s", libdir, v, bare ? "crt0_bare.o" : "crt0.o");
		for (k = 0; k < nobjs; k++) largv[n++] = objs[k];
		largv[n++] = fmt("%s/%s/%s", libdir, v, bare ? "sys_bare.o" : "sys.o");
		for (k = 0; libs[k]; k++) largv[n++] = fmt("%s/%s/%s", libdir, v, libs[k]);
		largv[n] = NULL;
		rc = run(largv);
	}
	cleanup();
	return rc;
}
