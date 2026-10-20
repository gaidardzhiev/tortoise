#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#define MAXARGS 32
#define MAXCMDS 8

typedef struct {
	char *argv[MAXARGS + 1];
	int argc;
	char *in;
	char *out;
	int append;
} Cmd;

static char line[512];
static int interactive = 1;
static char words[1024];

static int tokenize(char *s, char **tok, int max) {
	int n = 0;
	char *w = words;
	while (*s) {
		while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
		if (!*s) break;
		if (n >= max) return -1;
		tok[n++] = w;
		if (*s == '|' || *s == '<' || *s == '&') {
			*w++ = *s++;
		} else if (*s == '>') {
			*w++ = *s++;
			if (*s == '>') *w++ = *s++;
		} else {
			while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && *s != '|' && *s != '<' && *s != '>' && *s != '&') {
				if (*s == '"') {
					s++;
					while (*s && *s != '"') *w++ = *s++;
					if (*s == '"') s++;
				} else {
					*w++ = *s++;
				}
			}
		}
		*w++ = 0;
	}
	return n;
}

static int is_op(char *t) {
	return !strcmp(t, "|") || !strcmp(t, "<") || !strcmp(t, ">") || !strcmp(t, ">>") || !strcmp(t, "&");
}

static void run_cmd(Cmd *c) {
	char path[128];
	if (c->in) {
		int fd = open(c->in, O_RDONLY);
		if (fd < 0) {
			printf("sh: cannot open %s\n", c->in);
			_exit(1);
		}
		close(0);
		dup(fd);
		close(fd);
	}
	if (c->out) {
		int fd = open(c->out, O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC));
		if (fd < 0) {
			printf("sh: cannot create %s\n", c->out);
			_exit(1);
		}
		close(1);
		dup(fd);
		close(fd);
	}
	if (strchr(c->argv[0], '/')) strcpy(path, c->argv[0]);
	else snprintf(path, sizeof(path), "/bin/%s", c->argv[0]);
	execv(path, c->argv);
	printf("sh: %s: not found\n", c->argv[0]);
	_exit(127);
}

static int code(int st) {
	if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
	return WEXITSTATUS(st);
}

static void report(int pid, int st) {
	st = code(st);
	if (st >= 128) printf("sh: pid %d terminated, status %d\n", pid, st);
}

int main(int argc, char **argv) {
	char *tok[MAXARGS * 2];
	Cmd cmds[MAXCMDS];
	FILE *in = stdin;
	if (argc > 1) {
		in = fopen(argv[1], "r");
		if (!in) {
			printf("sh: cannot open %s\n", argv[1]);
			return 127;
		}
		interactive = 0;
	}
	if (interactive) {
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		signal(SIGTSTP, SIG_IGN);
		signal(SIGTTOU, SIG_IGN);
	}
	for (;;) {
		int n, i, nc, bg = 0, pids[MAXCMDS], prev = -1, bad = 0;
		char *s;
		if (interactive) printf("$ ");
		if (!fgets(line, sizeof(line), in)) return 0;
		for (s = line; *s == ' ' || *s == '\t'; s++) ;
		if (*s == '#') continue;
		n = tokenize(line, tok, MAXARGS * 2);
		if (n < 0) {
			printf("sh: too many words\n");
			continue;
		}
		if (n == 0) continue;
		if (!strcmp(tok[n - 1], "&")) {
			bg = 1;
			n--;
		}
		memset(cmds, 0, sizeof(cmds));
		nc = 1;
		for (i = 0; i < n && !bad; i++) {
			Cmd *c = &cmds[nc - 1];
			if (!strcmp(tok[i], "|")) {
				if (c->argc == 0 || nc == MAXCMDS) bad = 1;
				else nc++;
			} else if (!strcmp(tok[i], "<") || !strcmp(tok[i], ">") || !strcmp(tok[i], ">>")) {
				if (i + 1 >= n || is_op(tok[i + 1])) bad = 1;
				else if (tok[i][0] == '<') c->in = tok[++i];
				else {
					c->append = tok[i][1] == '>';
					c->out = tok[++i];
				}
			} else if (!strcmp(tok[i], "&")) {
				bad = 1;
			} else if (c->argc < MAXARGS) {
				c->argv[c->argc++] = tok[i];
			}
		}
		if (bad || cmds[nc - 1].argc == 0) {
			printf("sh: syntax error\n");
			continue;
		}
		if (nc == 1 && !strcmp(cmds[0].argv[0], "cd")) {
			if (chdir(cmds[0].argc > 1 ? cmds[0].argv[1] : "/") < 0) printf("sh: cd: cannot change directory\n");
			continue;
		}
		if (nc == 1 && !strcmp(cmds[0].argv[0], "exit")) return cmds[0].argc > 1 ? atoi(cmds[0].argv[1]) : 0;
		fflush(stdout);
		for (i = 0; i < nc; i++) {
			int p[2] = {-1, -1};
			if (i < nc - 1 && pipe(p) < 0) {
				printf("sh: pipe failed\n");
				break;
			}
			pids[i] = fork();
			if (pids[i] == 0) {
				if (interactive) {
					if (!bg) {
						signal(SIGINT, SIG_DFL);
						signal(SIGQUIT, SIG_DFL);
					}
					signal(SIGTSTP, SIG_DFL);
					signal(SIGTTOU, SIG_DFL);
				}
				if (prev >= 0) {
					close(0);
					dup(prev);
					close(prev);
				}
				if (p[1] >= 0) {
					close(1);
					dup(p[1]);
					close(p[1]);
					close(p[0]);
				}
				run_cmd(&cmds[i]);
			}
			if (prev >= 0) close(prev);
			if (p[1] >= 0) close(p[1]);
			prev = p[0];
			if (pids[i] < 0) {
				printf("sh: fork failed\n");
				break;
			}
		}
		if (prev >= 0) close(prev);
		if (bg) {
			printf("[%d]\n", pids[nc - 1]);
			continue;
		}
		{
			int left = nc;
			while (left > 0) {
				int st = 0, w = waitpid(-1, &st, WUNTRACED), k, mine = 0;
				if (w < 0) break;
				if (WIFSTOPPED(st)) {
					for (k = 0; k < nc; k++)
						if (pids[k] == w) {
							pids[k] = -1;
							left--;
						}
					printf("sh: pid %d stopped\n", w);
					continue;
				}
				for (k = 0; k < nc; k++)
					if (pids[k] == w) {
						mine = 1;
						pids[k] = -1;
					}
				if (mine) {
					left--;
					report(w, st);
				} else {
					printf("[%d] done, status %d\n", w, code(st));
				}
			}
		}
	}
}
