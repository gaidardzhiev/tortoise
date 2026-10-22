/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

unsigned ram_size;
static char bootstack[8192];
unsigned bootstack_top = (unsigned)&bootstack[8192];
static unsigned evt[64];

void kmain(unsigned ramsize) {
	int i;
	ram_size = ramsize ? ramsize : rd(SYS_RAMSIZE);
	kprintf("TORTOISE OS booting, %d KB RAM\n", ram_size / 1024);
	for (i = 0; i < 64; i++) evt[i] = (unsigned)&alltraps;
	set_evt((unsigned)evt);
	mm_init();
	set_ptbase(kernel_pgdir);
	set_ctrl(1 | 4);
	set_ksp(bootstack_top);
	disk_init();
	vfs_init();
	{
		char conf[256];
		Vnode *cv;
		int n = 0, k;
		if (vfs_resolve("/etc/kernel.conf", rootvn, 1, &cv) == 0) {
			n = vfs_read(cv, conf, 0, sizeof(conf) - 1);
			vput(cv);
		}
		if (n < 0) n = 0;
		conf[n] = 0;
		for (k = 0; k + 8 <= n; k++)
			if (strncmp(conf + k, "selftest", 8) == 0) {
				kmalloc_selftest();
				break;
			}
	}
	proc_init();
	console_init();
	wr(TIMER_PRESCALE, 1000);
	wr(TIMER_PERIOD, 10);
	wr(TIMER_CTRL, 1);
	net_init();
	wr(PIC_MASK, 0x23);
	user_init();
	scheduler();
}
