/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/select.h>
#include "cpu.h"
#include "dev.h"

#define POLL_INTERVAL 1000
#define RXBUF 4096
#define DISK_DELAY 1000
#define FB_INTERVAL 100000

uint64_t dev_next;
uint32_t pic_pending;
uint32_t pic_mask;

typedef struct {
	uint32_t legacy_ticks;
	uint32_t t_count, t_period, t_ctrl, t_prescale, t_frac, t_phase;
	uint64_t t_last;
	uint32_t u_ctrl;
	uint32_t d_sector[2], d_count[2], d_addr[2], d_status[2];
	int d_cmd[2];
	uint64_t d_done[2];
	uint32_t fb_addr, fb_ctrl, k_ctrl;
	uint64_t poll_at, fb_at;
} DevState;

static DevState ds;
static Device devices[16];
static int out_dirty;
static int has_display;

static FILE *in_file;
static int in_stdin;
static int in_eof;
static uint8_t rxbuf[RXBUF];
static int rx_head, rx_tail;
static int term_raw;
static struct termios term_saved;

static uint8_t *disk_mem;
static FILE *disk_file[2];
static uint32_t disk_nsec[2] = {DISK_NUM_SECTORS, 0};
static int det_clock;

#ifdef TORTOISE_SDL
int sdl_open(void);
void sdl_frame(uint8_t *ram, uint32_t ram_size, uint32_t fb_addr, uint32_t ctrl);
int sdl_key(void);
int sdl_key_avail(void);
#endif

void pic_raise(int line) {
	pic_pending |= 1u << line;
}

static uint32_t pic_rd(uint32_t off) {
	if (off == 0x00) return pic_pending;
	if (off == 0x04) return pic_mask;
	if (off == 0x0C) {
		uint32_t p = pic_pending & pic_mask;
		for (uint32_t n = 0; n < 8; n++)
			if (p & (1u << n)) return n;
		return 0xFFFFFFFF;
	}
	return 0;
}

static void pic_wr(uint32_t off, uint32_t v) {
	if (off == 0x04) pic_mask = v & 0xFF;
	else if (off == 0x08) pic_pending &= ~v;
}

static void timer_sync(void) {
	uint64_t el = cpu.icount - ds.t_last, tot, ticks;
	ds.t_last = cpu.icount;
	tot = ds.t_frac + el;
	ticks = tot / ds.t_prescale;
	ds.t_frac = (uint32_t)(tot % ds.t_prescale);
	ds.t_count += (uint32_t)ticks;
	if ((ds.t_ctrl & 1) && ds.t_period) {
		uint64_t ph = ds.t_phase + ticks;
		if (ph >= ds.t_period) pic_raise(IRQ_TIMER);
		ds.t_phase = (uint32_t)(ph % ds.t_period);
	}
}

static uint64_t timer_next(void) {
	if (!(ds.t_ctrl & 1) || !ds.t_period) return UINT64_MAX;
	return cpu.icount + (uint64_t)(ds.t_period - ds.t_phase) * ds.t_prescale - ds.t_frac;
}

static uint32_t timer_rd(uint32_t off) {
	timer_sync();
	if (off == 0x00) return ds.t_count;
	if (off == 0x04) return ds.t_period;
	if (off == 0x08) return ds.t_ctrl;
	if (off == 0x0C) return ds.t_prescale;
	return 0;
}

static void timer_wr(uint32_t off, uint32_t v) {
	timer_sync();
	if (off == 0x04) {
		ds.t_period = v;
		ds.t_phase = 0;
	} else if (off == 0x08) {
		ds.t_ctrl = v & 1;
		ds.t_phase = 0;
	} else if (off == 0x0C) {
		ds.t_prescale = v ? v : 1;
	}
	dev_next = cpu.icount;
}

void dev_term_restore(void) {
	if (term_raw) {
		tcsetattr(0, TCSANOW, &term_saved);
		term_raw = 0;
	}
}

static void term_sig(int s) {
	dev_term_restore();
	signal(s, SIG_DFL);
	raise(s);
}

static void term_setup(void) {
	struct termios t;
	if (term_raw || !isatty(0) || tcgetattr(0, &term_saved)) return;
	t = term_saved;
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSANOW, &t)) return;
	term_raw = 1;
	atexit(dev_term_restore);
	signal(SIGINT, term_sig);
	signal(SIGTERM, term_sig);
	signal(SIGHUP, term_sig);
}

void dev_flush(void) {
	if (out_dirty) {
		fflush(stdout);
		out_dirty = 0;
	}
}

void con_putc(int c) {
	putchar(c & 0xFF);
	out_dirty = 1;
}

static int con_avail(void) {
	if (in_file) {
		int c = fgetc(in_file);
		if (c == EOF) return 0;
		ungetc(c, in_file);
		return 1;
	}
	return rx_head != rx_tail;
}

static void uart_rx_check(void) {
	if ((ds.u_ctrl & 1) && con_avail()) pic_raise(IRQ_UART_RX);
}

static void stdin_poll(int wait) {
	fd_set fs;
	struct timeval tv = {0, 0};
	int got = 0;
	if (wait == 2) {
		tv.tv_usec = 10000;
		wait = 0;
	}
	if (!in_stdin || in_eof) return;
	while (((rx_tail + 1) % RXBUF) != rx_head) {
		FD_ZERO(&fs);
		FD_SET(0, &fs);
		if (select(1, &fs, NULL, NULL, wait ? NULL : &tv) <= 0) break;
		uint8_t c;
		ssize_t r = read(0, &c, 1);
		if (r <= 0) {
			in_eof = 1;
			break;
		}
		rxbuf[rx_tail] = c;
		rx_tail = (rx_tail + 1) % RXBUF;
		got = 1;
		wait = 0;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
	}
	if (got) uart_rx_check();
}

int con_getc(void) {
	int c;
	if (in_file) {
		c = fgetc(in_file);
		if (c == EOF) return -1;
	} else {
		if (rx_head == rx_tail) return -1;
		c = rxbuf[rx_head];
		rx_head = (rx_head + 1) % RXBUF;
	}
	uart_rx_check();
	return c;
}

int uart_set_input(const char *path) {
	if (in_file) fclose(in_file);
	in_file = fopen(path, "rb");
	in_stdin = 0;
	return in_file ? 0 : -1;
}

void uart_use_stdin(int on) {
	in_stdin = on;
	if (on) term_setup();
}

static uint32_t uart_rd(uint32_t off) {
	if (off == 0x00) {
		int c = con_getc();
		return c < 0 ? 0 : (uint32_t)c;
	}
	if (off == 0x04) return (con_avail() ? 1 : 0) | 2;
	if (off == 0x08) return ds.u_ctrl;
	return 0;
}

static void uart_wr(uint32_t off, uint32_t v) {
	if (off == 0x00) {
		con_putc(v & 0xFF);
		if (ds.u_ctrl & 2) pic_raise(IRQ_UART_TX);
	} else if (off == 0x08) {
		ds.u_ctrl = v & 3;
		uart_rx_check();
	}
}

void dev_set_det(int on) {
	det_clock = on;
}

int disk_open_unit(int u, const char *path) {
	long sz;
	disk_file[u] = fopen(path, "r+b");
	if (!disk_file[u]) return -1;
	fseek(disk_file[u], 0, SEEK_END);
	sz = ftell(disk_file[u]);
	disk_nsec[u] = (uint32_t)(sz / DISK_SECTOR_SIZE);
	return 0;
}

int disk_open(const char *path) {
	return disk_open_unit(0, path);
}

void disk_close(void) {
	int u;
	for (u = 0; u < 2; u++) {
		if (disk_file[u]) fclose(disk_file[u]);
		disk_file[u] = NULL;
	}
	disk_nsec[0] = DISK_NUM_SECTORS;
	disk_nsec[1] = 0;
}

uint32_t disk_size(void) {
	return disk_nsec[0];
}

static int disk_rd(int u, uint32_t sec, uint8_t *buf) {
	if (sec >= disk_nsec[u]) return -1;
	if (disk_file[u]) {
		if (fseek(disk_file[u], (long)sec * DISK_SECTOR_SIZE, SEEK_SET)) return -1;
		if (fread(buf, 1, DISK_SECTOR_SIZE, disk_file[u]) != DISK_SECTOR_SIZE) memset(buf, 0, DISK_SECTOR_SIZE);
		return 0;
	}
	memcpy(buf, disk_mem + sec * DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
	return 0;
}

static int disk_wr(int u, uint32_t sec, const uint8_t *buf) {
	if (sec >= disk_nsec[u]) return -1;
	if (disk_file[u]) {
		if (fseek(disk_file[u], (long)sec * DISK_SECTOR_SIZE, SEEK_SET)) return -1;
		if (fwrite(buf, 1, DISK_SECTOR_SIZE, disk_file[u]) != DISK_SECTOR_SIZE) return -1;
		fflush(disk_file[u]);
		return 0;
	}
	memcpy(disk_mem + sec * DISK_SECTOR_SIZE, buf, DISK_SECTOR_SIZE);
	return 0;
}

static void disk_complete(int u) {
	uint32_t n = ds.d_count[u], s = ds.d_sector[u], a = ds.d_addr[u];
	int err = 0;
	ds.d_status[u] &= ~1u;
	if ((uint64_t)s + n > disk_nsec[u]) err = 1;
	if ((uint64_t)a + (uint64_t)n * DISK_SECTOR_SIZE > cpu.ram_size) err = 1;
	if (!err) {
		for (uint32_t i = 0; i < n && !err; i++) {
			uint8_t *p = cpu.memory + a + i * DISK_SECTOR_SIZE;
			if (ds.d_cmd[u] == 1) {
				err = disk_rd(u, s + i, p) != 0;
				cpu_mem_written(a + i * DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
			} else {
				err = disk_wr(u, s + i, p) != 0;
			}
		}
	}
	ds.d_status[u] |= err ? 2 : 4;
	ds.d_cmd[u] = 0;
	pic_raise(u ? IRQ_DISK2 : IRQ_DISK);
}

static uint32_t disk_reg_rd(int u, uint32_t off) {
	switch (off) {
	case 0x00: return ds.d_sector[u];
	case 0x04: return ds.d_count[u];
	case 0x08: return ds.d_addr[u];
	case 0x0C: return (uint32_t)ds.d_cmd[u];
	case 0x10: return ds.d_status[u];
	case 0x14: return disk_nsec[u];
	}
	return 0;
}

static void disk_reg_wr(int u, uint32_t off, uint32_t v) {
	switch (off) {
	case 0x00: ds.d_sector[u] = v; break;
	case 0x04: ds.d_count[u] = v; break;
	case 0x08: ds.d_addr[u] = v; break;
	case 0x0C:
		if ((ds.d_status[u] & 1) || (v != 1 && v != 2)) break;
		ds.d_cmd[u] = (int)v;
		ds.d_status[u] = (ds.d_status[u] & ~6u) | 1;
		ds.d_done[u] = cpu.icount + (uint64_t)DISK_DELAY * ds.d_count[u];
		dev_next = cpu.icount;
		break;
	case 0x10: ds.d_status[u] &= ~6u; break;
	}
}

static uint32_t disk_dev_rd(uint32_t off) {
	return disk_reg_rd(0, off);
}

static void disk_dev_wr(uint32_t off, uint32_t v) {
	disk_reg_wr(0, off, v);
}

static uint32_t disk2_dev_rd(uint32_t off) {
	return disk_reg_rd(1, off);
}

static void disk2_dev_wr(uint32_t off, uint32_t v) {
	disk_reg_wr(1, off, v);
}

static uint32_t rtc_rd(uint32_t off) {
	if (off != 0) return 0;
	if (det_clock) return (uint32_t)(cpu.icount / 10000000u);
	return (uint32_t)time(NULL);
}

static void rtc_wr(uint32_t off, uint32_t v) {
	(void)off;
	(void)v;
}

static uint32_t fb_rd(uint32_t off) {
	if (!has_display) return 0;
	if (off == 0x00) return 640;
	if (off == 0x04) return 480;
	if (off == 0x08) return ds.fb_addr;
	if (off == 0x0C) return ds.fb_ctrl;
	return 0;
}

static void fb_wr(uint32_t off, uint32_t v) {
	if (!has_display) return;
	if (off == 0x08) ds.fb_addr = v;
	else if (off == 0x0C) ds.fb_ctrl = v & 3;
}

static uint32_t kbd_rd(uint32_t off) {
	if (!has_display) return 0;
#ifdef TORTOISE_SDL
	if (off == 0x00) return (uint32_t)sdl_key();
	if (off == 0x04) return sdl_key_avail() ? 1 : 0;
#endif
	if (off == 0x08) return ds.k_ctrl;
	return 0;
}

static void kbd_wr(uint32_t off, uint32_t v) {
	if (!has_display) return;
	if (off == 0x08) ds.k_ctrl = v & 1;
}

static uint32_t sys_rd(uint32_t off) {
	if (off == SYS_RAMSIZE) return cpu.ram_size;
	if (off == SYS_VERSION) return 2;
	return 0;
}

static void sys_wr(uint32_t off, uint32_t v) {
	if (off == SYS_POWEROFF) cpu_stop((int)(v & 0xFF));
}

static void dev_add(uint32_t base, uint32_t (*rd)(uint32_t), void (*wr)(uint32_t, uint32_t)) {
	Device *d = &devices[(base - DEV_BASE) >> 12];
	d->base = base;
	d->rd = rd;
	d->wr = wr;
}

int dev_access(uint32_t pa, uint32_t *v, int wr) {
	Device *d = &devices[((pa - DEV_BASE) >> 12) & 15];
	if (!d->rd) return 0;
	if (wr) d->wr(pa & 0xFFF, *v);
	else *v = d->rd(pa & 0xFFF);
	return 1;
}

static void schedule(void) {
	uint64_t n = ds.poll_at, t;
	if (cpu.ilimit < n) n = cpu.ilimit;
	t = timer_next();
	if (t < n) n = t;
	for (int u = 0; u < 2; u++)
		if ((ds.d_status[u] & 1) && ds.d_done[u] < n) n = ds.d_done[u];
	if (has_display && ds.fb_at < n) n = ds.fb_at;
	dev_next = n;
}

void dev_open_display(void) {
#ifdef TORTOISE_SDL
	has_display = sdl_open() == 0;
#endif
}

void dev_reset(void) {
	memset(devices, 0, sizeof(devices));
	memset(&ds, 0, sizeof(ds));
	pic_pending = 0;
	pic_mask = 0;
	ds.t_prescale = 1000;
	if (!disk_mem) disk_mem = malloc(DISK_SECTOR_SIZE * DISK_NUM_SECTORS);
	memset(disk_mem, 0, DISK_SECTOR_SIZE * DISK_NUM_SECTORS);
	dev_add(PIC_BASE, pic_rd, pic_wr);
	dev_add(TIMER_BASE, timer_rd, timer_wr);
	dev_add(UART_BASE, uart_rd, uart_wr);
	dev_add(DISK_BASE, disk_dev_rd, disk_dev_wr);
	dev_add(DISK2_BASE, disk2_dev_rd, disk2_dev_wr);
	dev_add(KBD_BASE, kbd_rd, kbd_wr);
	dev_add(RTC_BASE, rtc_rd, rtc_wr);
	dev_add(FB_BASE, fb_rd, fb_wr);
	dev_add(SYS_BASE, sys_rd, sys_wr);
	ds.poll_at = POLL_INTERVAL;
	ds.fb_at = FB_INTERVAL;
	schedule();
}

void dev_events(void) {
	if (cpu.icount >= cpu.ilimit) {
		cpu_stop(EXIT_LIMIT);
		return;
	}
	timer_sync();
	for (int u = 0; u < 2; u++)
		if ((ds.d_status[u] & 1) && cpu.icount >= ds.d_done[u]) disk_complete(u);
	if (cpu.icount >= ds.poll_at) {
		dev_flush();
		stdin_poll(0);
		ds.poll_at = cpu.icount + POLL_INTERVAL;
	}
#ifdef TORTOISE_SDL
	if (has_display && cpu.icount >= ds.fb_at) {
		sdl_frame(cpu.memory, cpu.ram_size, ds.fb_addr, ds.fb_ctrl);
		if ((ds.k_ctrl & 1) && sdl_key_avail()) pic_raise(IRQ_KBD);
		ds.fb_at = cpu.icount + FB_INTERVAL;
	}
#endif
	schedule();
}

static int input_wait_possible(void) {
	return in_stdin && !in_eof && (ds.u_ctrl & 1) && (pic_mask & (1u << IRQ_UART_RX));
}

void dev_wfi(void) {
	while (!(pic_pending & pic_mask) && !cpu.halted) {
		uint64_t t = UINT64_MAX;
		int inp = input_wait_possible() || (has_display && (ds.k_ctrl & 1) && (pic_mask & (1u << IRQ_KBD)));
		if (pic_mask & (1u << IRQ_TIMER)) t = timer_next();
		if ((pic_mask & (1u << IRQ_DISK)) && (ds.d_status[0] & 1) && ds.d_done[0] < t) t = ds.d_done[0];
		if ((pic_mask & (1u << IRQ_DISK2)) && (ds.d_status[1] & 1) && ds.d_done[1] < t) t = ds.d_done[1];
		if (t == UINT64_MAX && !inp) {
			dev_flush();
			fprintf(stderr, "WFI deadlock at PC=0x%08X\n", cpu.inst_pc);
			cpu_stop(EXIT_DEADLOCK);
			return;
		}
		if (inp) {
			dev_flush();
			if (in_stdin) stdin_poll(t == UINT64_MAX && !has_display ? 1 : 2);
			else usleep(10000);
			if (pic_pending & pic_mask) break;
		}
		if (t != UINT64_MAX) {
			if (t >= cpu.ilimit) {
				cpu.icount = cpu.ilimit - 1;
				cpu_stop(EXIT_LIMIT);
				return;
			}
			if (t > cpu.icount) cpu.icount = t;
			dev_events();
		} else if (has_display) {
			cpu.icount += FB_INTERVAL;
			dev_events();
		}
	}
}

int legacy_in(uint32_t port, uint32_t *v) {
	if (port == IO_CONSOLE_DATA) {
		int c = con_avail() ? con_getc() : -1;
		*v = c < 0 ? 'A' : (uint32_t)c;
	} else if (port == IO_CONSOLE_STATUS) {
		*v = 0x03;
	} else if (port == IO_TIMER) {
		*v = ++ds.legacy_ticks;
	} else if (port == IO_RNG) {
		*v = (uint32_t)(rand() & 0xFFFF);
	} else if (port == IO_SYS) {
		*v = cpu.halted;
	} else if (port == IO_DISK_SECTOR) {
		*v = cpu.disk_sector;
	} else if (port == IO_DISK_STATUS) {
		*v = 0x01;
	} else if (port == IO_DISK_BUFFER) {
		*v = cpu.disk_buffer;
	} else {
		return 0;
	}
	return 1;
}

void legacy_out(uint32_t port, uint32_t v) {
	uint8_t buf[DISK_SECTOR_SIZE];
	if (port == IO_CONSOLE_DATA) {
		con_putc(v & 0xFF);
		dev_flush();
	} else if (port == IO_DISK_SECTOR) {
		cpu.disk_sector = disk_nsec[0] == DISK_NUM_SECTORS ? (v & (DISK_NUM_SECTORS - 1)) : v;
	} else if (port == IO_DISK_BUFFER) {
		cpu.disk_buffer = v;
	} else if (port == IO_DISK_CMD) {
		if (cpu.disk_buffer > cpu.ram_size || cpu.ram_size - cpu.disk_buffer < DISK_SECTOR_SIZE) return;
		if (v == 1) {
			if (disk_rd(0, cpu.disk_sector, buf) == 0) {
				memcpy(&cpu.memory[cpu.disk_buffer], buf, DISK_SECTOR_SIZE);
				cpu_mem_written(cpu.disk_buffer, DISK_SECTOR_SIZE);
			}
		} else if (v == 2) {
			disk_wr(0, cpu.disk_sector, &cpu.memory[cpu.disk_buffer]);
		}
	}
}

void dev_save(FILE *f) {
	fwrite(&pic_pending, sizeof(pic_pending), 1, f);
	fwrite(&pic_mask, sizeof(pic_mask), 1, f);
	fwrite(&ds, sizeof(ds), 1, f);
}

int dev_load(FILE *f) {
	if (fread(&pic_pending, sizeof(pic_pending), 1, f) != 1) return -1;
	if (fread(&pic_mask, sizeof(pic_mask), 1, f) != 1) return -1;
	if (fread(&ds, sizeof(ds), 1, f) != 1) return -1;
	return 0;
}
