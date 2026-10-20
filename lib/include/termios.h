/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _TERMIOS_H
#define _TERMIOS_H
#define NCCS 16
#define ICRNL 0x100
#define OPOST 1
#define ONLCR 4
#define ISIG 1
#define ICANON 2
#define ECHO 8
#define ECHOE 0x10
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSUSP 10
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TIOCSCTTY 0x540E
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413
typedef unsigned tcflag_t;
typedef unsigned char cc_t;
struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[NCCS];
};
struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};
int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int how, const struct termios *t);
void cfmakeraw(struct termios *t);
int tcgetpgrp(int fd);
int tcsetpgrp(int fd, int pgrp);
#endif
