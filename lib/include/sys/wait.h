/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8
#define WIFEXITED(s) (((s) & 0x7F) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s) (((s) & 0x7F) != 0 && ((s) & 0x7F) != 0x7F)
#define WTERMSIG(s) ((s) & 0x7F)
#define WIFSTOPPED(s) (((s) & 0xFF) == 0x7F)
#define WSTOPSIG(s) (((s) >> 8) & 0xFF)
#define WIFCONTINUED(s) ((s) == 0xFFFF)
int wait(int *status);
int waitpid(int pid, int *status, int options);
#endif
