/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef DEV_H
#define DEV_H

#include <stdint.h>
#include <stdio.h>

#define SYS_BASE 0xF0007000u
#define SYS_POWEROFF 0x00
#define SYS_RAMSIZE 0x04
#define SYS_VERSION 0x08

typedef struct {
	uint32_t base;
	uint32_t (*rd)(uint32_t off);
	void (*wr)(uint32_t off, uint32_t v);
} Device;

extern uint64_t dev_next;
extern uint32_t pic_pending;
extern uint32_t pic_mask;

void dev_reset(void);
int dev_access(uint32_t pa, uint32_t *v, int wr);
void dev_events(void);
void dev_flush(void);
int legacy_in(uint32_t port, uint32_t *v);
void legacy_out(uint32_t port, uint32_t v);
void con_putc(int c);
int con_getc(void);
int uart_set_input(const char *path);
void uart_use_stdin(int on);
int disk_open(const char *path);
int disk_open_unit(int u, const char *path);
void disk_close(void);
uint32_t disk_size(void);
void dev_set_det(int on);
void dev_term_restore(void);
void dev_wfi(void);
void dev_open_display(void);
void dev_save(FILE *f);
int dev_load(FILE *f);
void pic_raise(int line);

#define PIC_BASE 0xF0000000u
#define TIMER_BASE 0xF0001000u
#define UART_BASE 0xF0002000u
#define DISK_BASE 0xF0003000u
#define KBD_BASE 0xF0004000u
#define RTC_BASE 0xF0005000u
#define FB_BASE 0xF0006000u
#define IRQ_TIMER 0
#define IRQ_UART_RX 1
#define IRQ_UART_TX 2
#define IRQ_DISK 3
#define IRQ_KBD 4
#define IRQ_NET 5
#define IRQ_DISK2 6
#define DISK2_BASE 0xF0009000u
#define NET_BASE 0xF0008000u

#endif
