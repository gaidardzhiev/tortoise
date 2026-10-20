/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "cpu.h"
#include "dev.h"
#include "dis.h"

CPU cpu;
int cpu_verbose = 0;

#define VLOG(...) do { if (cpu_verbose) printf(__VA_ARGS__); } while (0)

static uint8_t *ram_buf;
static uint32_t ram_buf_size;
static uint8_t rom[ROM_SIZE];
static int rom_present;
static jmp_buf fault_jb;
static int jb_set;
static FILE *trace_f;
static void trace_begin(void);

typedef struct {
	uint8_t len;
	uint8_t b[7];
} DEnt;

static DEnt *dc_tab[65536];
static int dc_on = 1;
static const uint8_t *ib;
static int ipos;
static uint8_t op_len[256];

static inline uint32_t rd16(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static inline uint32_t rd32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void wr32(uint8_t *p, uint32_t v) {
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

static void cpu_abort(void) {
	if (!jb_set) {
		dev_flush();
		exit(cpu.exit_code);
	}
	longjmp(fault_jb, 1);
}

void cpu_stop(int code) {
	cpu.halted = 1;
	cpu.exit_code = code;
}

void cpu_bus_error(uint32_t addr) {
	if (!(cpu.cr[CR_CTRL] & CR0_EXT)) {
		dev_flush();
		fprintf(stderr, "bus error at 0x%08X (PC=0x%08X)\n", addr, cpu.inst_pc);
		cpu_stop(EXIT_INVALID);
		cpu_abort();
	}
	cpu.cr[CR_FADDR] = addr;
	cpu_fault(VEC_BUS, 0);
}

static uint32_t vm_read(uint32_t va, int n, int user);
static void vm_write(uint32_t va, uint32_t v, int n, int user);
static void fetch_inval(void);

static uint32_t sup_read(uint32_t va) {
	return vm_read(va, 4, 0);
}

static void sup_write(uint32_t va, uint32_t v) {
	vm_write(va, v, 4, 0);
}

static void enter(uint32_t vec, uint32_t savepc, uint32_t code) {
	uint32_t of = cpu_get_flags(), sp = cpu.SP, h;
	if (cpu.mode == MODE_USER) {
		sp = cpu.cr[CR_KSP] - 4;
		sup_write(sp, cpu.SP);
	}
	sup_write(sp - 4, of);
	sup_write(sp - 8, savepc);
	h = sup_read(cpu.cr[CR_EVT] + 4 * vec);
	cpu.SP = sp - 8;
	cpu.cr[CR_CAUSE] = (vec & 0xFF) | ((code & 0xFF) << 8);
	cpu.mode = MODE_SUPERVISOR;
	cpu.int_enabled = 0;
	cpu.PC = h;
	fetch_inval();
	VLOG("exception: vector=%u PC=0x%X\n", vec, h);
}

void cpu_deliver(uint32_t vec, uint32_t savepc, uint32_t code) {
	if (cpu.depth) {
		cpu_fault(vec, code);
		return;
	}
	cpu.depth = 1;
	enter(vec, savepc, code);
	cpu.depth = 0;
}

void cpu_fault(uint32_t vec, uint32_t code) {
	cpu.PC = cpu.inst_pc;
	if (cpu.depth == 0) {
		cpu.depth = 1;
		enter(vec, cpu.inst_pc, code);
	} else if (cpu.depth == 1) {
		cpu.depth = 2;
		enter(VEC_DF, cpu.inst_pc, 0);
	} else {
		dev_flush();
		fprintf(stderr, "triple fault: PC=0x%08X CAUSE=0x%08X vector=%u\n", cpu.inst_pc, cpu.cr[CR_CAUSE], vec);
		cpu_stop(EXIT_TRIPLE);
	}
	cpu.depth = 0;
	cpu_abort();
}

uint32_t phys_read(uint32_t pa, int n) {
	if (pa < cpu.ram_size && cpu.ram_size - pa >= (uint32_t)n) {
		uint8_t *p = cpu.memory + pa;
		return n == 1 ? p[0] : n == 2 ? rd16(p) : rd32(p);
	}
	if (pa >= ROM_BASE && rom_present) {
		uint32_t o = pa - ROM_BASE;
		if (ROM_SIZE - o >= (uint32_t)n) {
			uint8_t *p = rom + o;
			return n == 1 ? p[0] : n == 2 ? rd16(p) : rd32(p);
		}
	}
	if (pa >= DEV_BASE && pa < DEV_END && n == 4 && !(pa & 3)) {
		uint32_t v;
		if (dev_access(pa, &v, 0)) return v;
	}
	cpu_bus_error(pa);
	return 0;
}

void phys_write(uint32_t pa, uint32_t v, int n) {
	if (pa < cpu.ram_size && cpu.ram_size - pa >= (uint32_t)n) {
		uint8_t *p = cpu.memory + pa;
		if (dc_tab[pa >> 12] || dc_tab[(pa + n - 1) >> 12]) cpu_mem_written(pa, (uint32_t)n);
		p[0] = v & 0xFF;
		if (n > 1) p[1] = (v >> 8) & 0xFF;
		if (n > 2) {
			p[2] = (v >> 16) & 0xFF;
			p[3] = (v >> 24) & 0xFF;
		}
		return;
	}
	if (pa >= DEV_BASE && pa < DEV_END && n == 4 && !(pa & 3)) {
		if (dev_access(pa, &v, 1)) return;
	}
	cpu_bus_error(pa);
}

#define ACC_R 0
#define ACC_W 1
#define ACC_X 2

typedef struct {
	uint32_t gen;
	uint32_t pte;
	uint32_t pte_pa;
} TlbEnt;

static TlbEnt *tlb_dir[1024];
static uint32_t tlb_gen = 1;
static uint32_t f_tag = 1;
static uint8_t *f_ptr;
static int32_t f_ppn = -1;

static void fetch_inval(void) {
	f_tag = 1;
}

void cpu_flush_tlb(void) {
	if (++tlb_gen == 0) {
		for (int i = 0; i < 1024; i++)
			if (tlb_dir[i]) memset(tlb_dir[i], 0, 1024 * sizeof(TlbEnt));
		tlb_gen = 1;
	}
	fetch_inval();
}

void cpu_invlpg(uint32_t va) {
	TlbEnt *d = tlb_dir[va >> 22];
	if (d) d[(va >> 12) & 0x3FF].gen = 0;
	fetch_inval();
}

static TlbEnt *tlb_slot(uint32_t va) {
	TlbEnt *d = tlb_dir[va >> 22];
	if (!d) {
		d = calloc(1024, sizeof(TlbEnt));
		if (!d) {
			fprintf(stderr, "out of memory\n");
			exit(EXIT_FATAL);
		}
		tlb_dir[va >> 22] = d;
	}
	return &d[(va >> 12) & 0x3FF];
}

static void page_fault(uint32_t va, int acc, int user, int present) {
	uint32_t code = (present ? PF_P : 0) | (acc == ACC_W ? PF_W : 0) | (user ? PF_U : 0) | (acc == ACC_X ? PF_I : 0);
	cpu.cr[CR_FADDR] = va;
	cpu_fault(VEC_PF, code);
}

static int pte_denied(uint32_t pte, int acc, int user) {
	if (user && !(pte & PTE_U)) return 1;
	if (acc == ACC_W && !(pte & PTE_W)) return 1;
	if (acc == ACC_X && !(pte & PTE_X)) return 1;
	return 0;
}

static uint32_t pg_translate(uint32_t va, int acc, int user) {
	TlbEnt *e = tlb_slot(va);
	if (e->gen != tlb_gen) {
		uint32_t pde = phys_read((cpu.cr[CR_PTBASE] & ~0xFFFu) + (va >> 22) * 4, 4);
		uint32_t pte_pa, pte, npte;
		if (!(pde & PTE_P)) page_fault(va, acc, user, 0);
		pte_pa = (pde & ~0xFFFu) + ((va >> 12) & 0x3FF) * 4;
		pte = phys_read(pte_pa, 4);
		if (!(pte & PTE_P)) page_fault(va, acc, user, 0);
		if (pte_denied(pte, acc, user)) page_fault(va, acc, user, 1);
		npte = pte | PTE_A | (acc == ACC_W ? PTE_D : 0);
		if (npte != pte) phys_write(pte_pa, npte, 4);
		e->gen = tlb_gen;
		e->pte = npte;
		e->pte_pa = pte_pa;
		return (npte & ~0xFFFu) | (va & 0xFFF);
	}
	if (pte_denied(e->pte, acc, user)) page_fault(va, acc, user, 1);
	if (acc == ACC_W && !(e->pte & PTE_D)) {
		e->pte |= PTE_D;
		phys_write(e->pte_pa, phys_read(e->pte_pa, 4) | PTE_D | PTE_A, 4);
	}
	return (e->pte & ~0xFFFu) | (va & 0xFFF);
}

static inline int xl_active(void) {
	uint32_t c0 = cpu.cr[CR_CTRL];
	return (c0 & CR0_EXT) && (c0 & (CR0_PG | CR0_PROT));
}

static uint32_t translate(uint32_t va, int acc, int user) {
	uint32_t c0 = cpu.cr[CR_CTRL];
	if (!(c0 & CR0_EXT)) return va;
	if (c0 & CR0_PG) return pg_translate(va, acc, user);
	if ((c0 & CR0_PROT) && user) {
		if (va >= cpu.cr[CR_LIMIT]) {
			cpu.cr[CR_FADDR] = va;
			cpu_fault(VEC_GP, 0);
		}
		return cpu.cr[CR_BASE] + va;
	}
	return va;
}

static uint32_t vm_read(uint32_t va, int n, int user) {
	uint32_t pa;
	if (!xl_active()) return phys_read(va, n);
	if (n > 1 && ((va & 0xFFF) + n > 0x1000 || ((cpu.cr[CR_CTRL] & CR0_PG) == 0 && user))) {
		uint32_t p[4], v = 0;
		for (int i = 0; i < n; i++) p[i] = translate(va + i, ACC_R, user);
		if (n == 4 && p[3] == p[0] + 3) return phys_read(p[0], 4);
		if (n == 2 && p[1] == p[0] + 1) return phys_read(p[0], 2);
		for (int i = 0; i < n; i++) v |= phys_read(p[i], 1) << (8 * i);
		return v;
	}
	pa = translate(va, ACC_R, user);
	return phys_read(pa, n);
}

static void vm_write(uint32_t va, uint32_t v, int n, int user) {
	uint32_t pa;
	if (!xl_active()) {
		phys_write(va, v, n);
		return;
	}
	if (n > 1 && ((va & 0xFFF) + n > 0x1000 || ((cpu.cr[CR_CTRL] & CR0_PG) == 0 && user))) {
		uint32_t p[4];
		for (int i = 0; i < n; i++) p[i] = translate(va + i, ACC_W, user);
		if (n == 4 && p[3] == p[0] + 3) {
			phys_write(p[0], v, 4);
			return;
		}
		if (n == 2 && p[1] == p[0] + 1) {
			phys_write(p[0], v, 2);
			return;
		}
		for (int i = 0; i < n; i++) phys_write(p[i], (v >> (8 * i)) & 0xFF, 1);
		return;
	}
	pa = translate(va, ACC_W, user);
	phys_write(pa, v, n);
}

uint32_t mem_read(uint32_t va, int n) {
	if (!(cpu.cr[CR_CTRL] & CR0_EXT) && cpu.ram_size - va >= (uint32_t)n && va < cpu.ram_size) {
		uint8_t *p = cpu.memory + va;
		return n == 1 ? p[0] : n == 2 ? rd16(p) : rd32(p);
	}
	return vm_read(va, n, cpu.mode == MODE_USER);
}

void mem_write(uint32_t va, uint32_t v, int n) {
	vm_write(va, v, n, cpu.mode == MODE_USER);
}

int mem_peek(uint32_t va, uint8_t *b) {
	uint32_t pa = va;
	if (xl_active() && (cpu.cr[CR_CTRL] & CR0_PG)) {
		uint32_t pde, pte;
		if ((cpu.cr[CR_PTBASE] & ~0xFFFu) + 4096 > cpu.ram_size) return 0;
		pde = rd32(cpu.memory + (cpu.cr[CR_PTBASE] & ~0xFFFu) + (va >> 22) * 4);
		if (!(pde & PTE_P) || (pde & ~0xFFFu) + 4096 > cpu.ram_size) return 0;
		pte = rd32(cpu.memory + (pde & ~0xFFFu) + ((va >> 12) & 0x3FF) * 4);
		if (!(pte & PTE_P)) return 0;
		pa = (pte & ~0xFFFu) | (va & 0xFFF);
	} else if (xl_active() && cpu.mode == MODE_USER) {
		if (va >= cpu.cr[CR_LIMIT]) return 0;
		pa = cpu.cr[CR_BASE] + va;
	}
	if (pa < cpu.ram_size) {
		*b = cpu.memory[pa];
		return 1;
	}
	if (pa >= ROM_BASE && rom_present) {
		*b = rom[pa - ROM_BASE];
		return 1;
	}
	return 0;
}

static uint8_t fetch_slow(uint32_t pc) {
	uint32_t pa, pb;
	int user = cpu.mode == MODE_USER;
	if (xl_active() && !(cpu.cr[CR_CTRL] & CR0_PG) && user) {
		pa = translate(pc, ACC_X, 1);
		return (uint8_t)phys_read(pa, 1);
	}
	pa = translate(pc, ACC_X, user);
	pb = pa & ~0xFFFu;
	if (pb < cpu.ram_size && cpu.ram_size - pb >= 0x1000) {
		f_tag = pc & ~0xFFFu;
		f_ptr = cpu.memory + pb;
		f_ppn = (int32_t)(pb >> 12);
		return f_ptr[pc & 0xFFF];
	}
	if (pb >= ROM_BASE && rom_present) {
		f_tag = pc & ~0xFFFu;
		f_ptr = rom + (pb - ROM_BASE);
		f_ppn = -1;
		return f_ptr[pc & 0xFFF];
	}
	return (uint8_t)phys_read(pa, 1);
}

static inline uint8_t fetch_at(uint32_t pc) {
	if ((pc & ~0xFFFu) == f_tag) return f_ptr[pc & 0xFFF];
	return fetch_slow(pc);
}

static inline uint8_t fetch_byte(void) {
	return ib[ipos++];
}

static inline uint16_t fetch_word(void) {
	uint16_t v = (uint16_t)(ib[ipos] | (ib[ipos + 1] << 8));
	ipos += 2;
	return v;
}

static inline uint32_t fetch_dword(void) {
	uint32_t v = rd32(ib + ipos);
	ipos += 4;
	return v;
}

static void decode(uint32_t pc, DEnt *e) {
	uint8_t op = fetch_at(pc);
	int len = op_len[op], i;
	e->b[0] = op;
	for (i = 1; i < len; i++) e->b[i] = fetch_at(pc + i);
	e->len = (uint8_t)len;
}

void cpu_dc_flush(void) {
	for (int i = 0; i < 65536; i++) {
		free(dc_tab[i]);
		dc_tab[i] = NULL;
	}
}

void cpu_set_dcache(int on) {
	dc_on = on;
	cpu_dc_flush();
}

void cpu_mem_written(uint32_t pa, uint32_t n) {
	uint32_t a, b;
	if (n == 0) return;
	a = pa >> 12;
	b = (pa + n - 1) >> 12;
	for (; a <= b && a < 65536; a++) {
		if (dc_tab[a]) {
			free(dc_tab[a]);
			dc_tab[a] = NULL;
		}
	}
}

void cpu_init_size(uint32_t ram_size) {
	memset(&cpu, 0, sizeof(cpu));
	if (ram_buf_size != ram_size) {
		free(ram_buf);
		ram_buf = malloc(ram_size);
		if (!ram_buf) {
			fprintf(stderr, "cannot allocate %u bytes of RAM\n", ram_size);
			exit(EXIT_FATAL);
		}
		ram_buf_size = ram_size;
	}
	memset(ram_buf, 0, ram_size);
	cpu.memory = ram_buf;
	cpu.ram_size = ram_size;
	cpu.cr[CR_RAMSIZE] = ram_size;
	cpu.PC = 0;
	cpu.SP = ram_size;
	cpu.halted = 0;
	cpu.int_enabled = 1;
	cpu.mode = MODE_SUPERVISOR;
	cpu.ilimit = UINT64_MAX;
	cpu_flush_tlb();
	cpu_dc_flush();
	if (!op_len[0]) {
		for (int i = 0; i < 256; i++) op_len[i] = dis_ops[i].name ? (uint8_t)dis_len[dis_ops[i].fmt] : 1;
	}
	dev_reset();
	VLOG("cpu_init: PC=0x%X SP=0x%X\n", cpu.PC, cpu.SP);
}

void cpu_init(void) {
	cpu_init_size(MEMORY_SIZE);
}

void load_program(const uint8_t *program, size_t size) {
	if (size > cpu.ram_size) {
		fprintf(stderr, "program too large\n");
		return;
	}
	memcpy(cpu.memory, program, size);
}

int rom_load(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	memset(rom, 0, sizeof(rom));
	size_t n = fread(rom, 1, ROM_SIZE, f);
	fclose(f);
	if (n == 0) return -1;
	rom_present = 1;
	return 0;
}

uint32_t cpu_get_flags(void) {
	return (cpu.flags & 0x0F) | (cpu.int_enabled ? FLAG_I : 0) | (cpu.mode ? FLAG_U : 0);
}

void cpu_set_flags(uint32_t f) {
	cpu.flags = f & 0x0F;
	cpu.int_enabled = (f & FLAG_I) ? 1 : 0;
	cpu.mode = (f & FLAG_U) ? MODE_USER : MODE_SUPERVISOR;
	fetch_inval();
}

static void push_word(uint16_t val) {
	uint32_t sp = cpu.SP - 2;
	mem_write(sp, val, 2);
	cpu.SP = sp;
	VLOG("push_word: SP=0x%X val=0x%X\n", cpu.SP, val);
}

static uint16_t pop_word(void) {
	uint16_t val = (uint16_t)mem_read(cpu.SP, 2);
	cpu.SP += 2;
	VLOG("pop_word: SP=0x%X val=0x%X\n", cpu.SP, val);
	return val;
}

void cpu_interrupt(uint8_t vector) {
	push_word(cpu.flags);
	push_word((uint16_t)cpu.PC);
	cpu.int_enabled = 0;
	uint8_t old_mode = cpu.mode;
	cpu.mode = MODE_SUPERVISOR;
	cpu.PC = mem_read(IVT_BASE + (uint32_t)vector * 2, 2);
	VLOG("interrupt: vector=%u PC=0x%X prev_mode=%u\n", vector, cpu.PC, old_mode);
}

static void set_flags(uint32_t a, uint32_t b, uint32_t result, uint8_t is_sub) {
	uint16_t r16 = result & 0xFFFF;
	cpu.flags = 0;
	if (r16 == 0)
		cpu.flags |= FLAG_Z;
	if (is_sub ? (b > a) : (result > 0xFFFF))
		cpu.flags |= FLAG_C;
	if (r16 & 0x8000)
		cpu.flags |= FLAG_S;
	uint8_t sa = (a >> 15) & 1;
	uint8_t sb = (b >> 15) & 1;
	uint8_t sr = (r16 >> 15) & 1;
	if (is_sub) {
		if (sa != sb && sr != sa)
			cpu.flags |= FLAG_V;
	} else {
		if (sa == sb && sr != sa)
			cpu.flags |= FLAG_V;
	}
}

static void invalid_opcode(uint8_t opcode) {
	if (!(cpu.cr[CR_CTRL] & CR0_EXT)) {
		dev_flush();
		fprintf(stderr, "unknown opcode 0x%X at PC=0x%X\n", opcode, cpu.inst_pc);
		cpu_stop(EXIT_INVALID);
		cpu_abort();
	}
	cpu_fault(VEC_UD, 0);
}

#define EXT_ON (cpu.cr[CR_CTRL] & CR0_EXT)

static inline void setzscv(uint32_t r, int c, int v) {
	cpu.flags = (cpu.flags & 0xF0) | (r == 0 ? FLAG_Z : 0) | ((r & 0x80000000u) ? FLAG_S : 0) | (c ? FLAG_C : 0) | (v ? FLAG_V : 0);
}

static inline void setzs(uint32_t r) {
	cpu.flags = (cpu.flags & ~(FLAG_Z | FLAG_S)) | (r == 0 ? FLAG_Z : 0) | ((r & 0x80000000u) ? FLAG_S : 0);
}

static uint32_t alu_add(uint32_t a, uint32_t b, uint32_t c) {
	uint64_t s = (uint64_t)a + b + c;
	uint32_t r = (uint32_t)s;
	setzscv(r, (s >> 32) != 0, ((~(a ^ b) & (a ^ r)) >> 31) & 1);
	return r;
}

static uint32_t alu_sub(uint32_t a, uint32_t b, uint32_t c) {
	uint32_t r = a - b - c;
	setzscv(r, (uint64_t)a < (uint64_t)b + c, (((a ^ b) & (a ^ r)) >> 31) & 1);
	return r;
}

static uint32_t alu_shift(int kind, uint32_t a, uint32_t n) {
	uint32_t r;
	int c;
	n &= 31;
	if (n == 0) {
		setzs(a);
		return a;
	}
	switch (kind) {
	case 0: r = a << n; c = (a >> (32 - n)) & 1; break;
	case 1: r = a >> n; c = (a >> (n - 1)) & 1; break;
	case 2: r = (uint32_t)((int32_t)a >> n); c = (a >> (n - 1)) & 1; break;
	case 3: r = (a << n) | (a >> (32 - n)); c = r & 1; break;
	default: r = (a >> n) | (a << (32 - n)); c = r >> 31; break;
	}
	setzscv(r, c, 0);
	return r;
}

static uint32_t alu_imul(uint32_t a, uint32_t b) {
	int64_t p = (int64_t)(int32_t)a * (int32_t)b;
	uint32_t r = (uint32_t)p;
	int ov = p != (int64_t)(int32_t)r;
	setzscv(r, ov, ov);
	return r;
}

static uint32_t alu_mulh(uint32_t a, uint32_t b, int sgn) {
	uint32_t r;
	if (sgn) r = (uint32_t)((uint64_t)((int64_t)(int32_t)a * (int32_t)b) >> 32);
	else r = (uint32_t)(((uint64_t)a * b) >> 32);
	setzscv(r, 0, 0);
	return r;
}

static void div_error(void) {
	if (EXT_ON) cpu_fault(VEC_DIV, 0);
	cpu_interrupt(0);
}

static int alu_div(int kind, uint32_t a, uint32_t b, uint32_t *out) {
	uint32_t r;
	if (b == 0 || (kind != 1 && kind != 3 && a == 0x80000000u && b == 0xFFFFFFFFu)) {
		div_error();
		return 0;
	}
	switch (kind) {
	case 0: r = (uint32_t)((int32_t)a / (int32_t)b); break;
	case 1: r = a % b; break;
	case 2: r = (uint32_t)((int32_t)a % (int32_t)b); break;
	default: r = a / b; break;
	}
	setzscv(r, 0, 0);
	*out = r;
	return 1;
}

static int cond(uint8_t op) {
	int z = (cpu.flags & FLAG_Z) != 0;
	int c = (cpu.flags & FLAG_C) != 0;
	int s = (cpu.flags & FLAG_S) != 0;
	int v = (cpu.flags & FLAG_V) != 0;
	switch (op) {
	case OP_BEQ: return z;
	case OP_BNE: return !z;
	case OP_BLT: return s != v;
	case OP_BGE: return s == v;
	case OP_BLE: return z || s != v;
	case OP_BGT: return !z && s == v;
	case OP_BLO: return c;
	case OP_BHS: return !c;
	case OP_BLS: return c || z;
	case OP_BHI: return !c && !z;
	case OP_BMI: return s;
	case OP_BPL: return !s;
	case OP_BVS: return v;
	default: return !v;
	}
}

static void push32(uint32_t v) {
	uint32_t sp = cpu.SP - 4;
	mem_write(sp, v, 4);
	cpu.SP = sp;
}

static uint32_t pop32(void) {
	uint32_t v = mem_read(cpu.SP, 4);
	cpu.SP += 4;
	return v;
}

static void priv(void) {
	if (EXT_ON && cpu.mode == MODE_USER) cpu_fault(VEC_GP, 0);
}

static void trap(uint32_t vec) {
	if (EXT_ON) cpu_deliver(vec, cpu.PC, 0);
	else cpu_interrupt((uint8_t)vec);
}

static void ext_iret(void) {
	uint32_t sp = cpu.SP;
	uint32_t pc = mem_read(sp, 4);
	uint32_t fl = mem_read(sp + 4, 4);
	sp += 8;
	if (fl & FLAG_U) sp = mem_read(sp, 4);
	cpu.PC = pc;
	cpu_set_flags(fl);
	cpu.SP = sp;
}

static void cr_write(uint32_t n, uint32_t v) {
	if (n == CR_RAMSIZE) return;
	cpu.cr[n] = v;
	if (n == CR_CTRL || n == CR_PTBASE) cpu_flush_tlb();
	if (n == CR_BASE || n == CR_LIMIT) cpu_flush_tlb();
}

static void execute_instruction(void) {
	uint8_t opcode = fetch_byte();
	VLOG("executing opcode 0x%X at PC=0x%X\n", opcode, cpu.inst_pc);
	if (cpu.mode == MODE_USER) {
		if (EXT_ON) {
			if (opcode == OP_CLI || opcode == OP_STI || opcode == OP_IRET || opcode == OP_IN || opcode == OP_OUT || opcode == OP_HALT || opcode == OP_SYSRET)
				cpu_fault(VEC_GP, 0);
		} else if (opcode == OP_CLI || opcode == OP_STI || opcode == OP_IRET || opcode == OP_IN || opcode == OP_OUT) {
			VLOG("privilege violation in user mode\n");
			cpu_interrupt(FAULT_GP);
			return;
		}
	}
	uint8_t reg, pair, dst, src;
	uint32_t val, addr, val32, addr32, eff;
	int16_t off;
	switch (opcode) {
	case OP_NOP:
		break;
	case OP_LOAD:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) cpu.registers[reg] = val;
		break;
	case OP_ADD:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t result = a + val;
			set_flags(a, val, result, 0);
			cpu.registers[reg] = result & 0xFFFF;
		}
		break;
	case OP_STORE:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8) mem_write(addr, cpu.registers[reg], 2);
		break;
	case OP_JMP:
		addr = fetch_word();
		cpu.PC = addr;
		break;
	case OP_JZ:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8 && cpu.registers[reg] == 0) cpu.PC = addr;
		break;
	case OP_AND:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			VLOG("AND before: R%d=0x%X val=0x%X\n", reg, cpu.registers[reg], val);
			cpu.registers[reg] &= val;
			VLOG("AND after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_OR:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			VLOG("OR before: R%d=0x%X val=0x%X\n", reg, cpu.registers[reg], val);
			cpu.registers[reg] |= val;
			VLOG("OR after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_XOR:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			VLOG("XOR before: R%d=0x%X val=0x%X\n", reg, cpu.registers[reg], val);
			cpu.registers[reg] ^= val;
			VLOG("XOR after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_NOT:
		reg = fetch_byte();
		if (reg < 8) {
			VLOG("NOT before: R%d=0x%X\n", reg, cpu.registers[reg]);
			cpu.registers[reg] = (uint16_t)(~cpu.registers[reg]);
			VLOG("NOT after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_CALL:
		addr = fetch_word();
		push_word((uint16_t)cpu.PC);
		VLOG("CALL to 0x%X from 0x%X\n", addr, cpu.PC);
		cpu.PC = addr;
		break;
	case OP_RET:
		addr = pop_word();
		VLOG("RET to 0x%X\n", addr);
		cpu.PC = addr;
		break;
	case OP_IN:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8) {
			if (legacy_in(addr, &val)) cpu.registers[reg] = val;
		}
		break;
	case OP_OUT:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8) {
			if (addr == IO_TIMER) {
				if (cpu.int_enabled) trap(1);
			} else if (addr == IO_SYS) {
				cpu.halted = 1;
			} else {
				legacy_out(addr, cpu.registers[reg]);
			}
		}
		break;
	case OP_SUB:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t result = (a - val) & 0xFFFF;
			set_flags(a, val, result, 1);
			cpu.registers[reg] = result;
			VLOG("SUB: R%d=0x%X - 0x%X = 0x%X flags=0x%X\n", reg, a, val, result, cpu.flags);
		}
		break;
	case OP_CMP:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t result = (a - val) & 0xFFFF;
			set_flags(a, val, result, 1);
			VLOG("CMP: R%d=0x%X vs 0x%X flags=0x%X\n", reg, a, val, cpu.flags);
		}
		break;
	case OP_JC:
		addr = fetch_word();
		if (cpu.flags & FLAG_C) cpu.PC = addr;
		break;
	case OP_JN:
		addr = fetch_word();
		if (cpu.flags & FLAG_S) cpu.PC = addr;
		break;
	case OP_JO:
		addr = fetch_word();
		if (cpu.flags & FLAG_V) cpu.PC = addr;
		break;
	case OP_JNZ:
		addr = fetch_word();
		if (!(cpu.flags & FLAG_Z)) cpu.PC = addr;
		break;
	case OP_SHL:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t res = (a << (val & 31)) & 0xFFFF;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (val > 0 && val <= 16 && ((a << (val - 1)) & 0x8000)) cpu.flags |= FLAG_C;
			if (res & 0x8000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
			VLOG("SHL: R%d=0x%X << %u = 0x%X flags=0x%X\n", reg, a, val, res, cpu.flags);
		}
		break;
	case OP_SHR:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t res = (a >> (val & 31)) & 0xFFFF;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (val > 0 && val <= 16 && ((a >> (val - 1)) & 1)) cpu.flags |= FLAG_C;
			if (res & 0x8000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
			VLOG("SHR: R%d=0x%X >> %u = 0x%X flags=0x%X\n", reg, a, val, res, cpu.flags);
		}
		break;
	case OP_MUL:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t prod = a * val;
			uint32_t res = prod & 0xFFFF;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (prod > 0xFFFF) cpu.flags |= FLAG_C | FLAG_V;
			if (res & 0x8000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
			VLOG("MUL: R%d=0x%X * 0x%X = 0x%X flags=0x%X\n", reg, a, val, res, cpu.flags);
		}
		break;
	case OP_DIV:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			if (val == 0) {
				VLOG("division by zero\n");
				cpu_interrupt(0);
			} else {
				uint32_t a = cpu.registers[reg];
				uint32_t res = (a / val) & 0xFFFF;
				cpu.flags = 0;
				if (res == 0) cpu.flags |= FLAG_Z;
				if (res & 0x8000) cpu.flags |= FLAG_S;
				cpu.registers[reg] = res;
				VLOG("DIV: R%d=0x%X / 0x%X = 0x%X flags=0x%X\n", reg, a, val, res, cpu.flags);
			}
		}
		break;
	case OP_INT:
		val = fetch_byte();
		if (EXT_ON) {
			if (val >= 64 || (cpu.mode == MODE_USER && val < 48)) cpu_fault(VEC_GP, 0);
			cpu_deliver(val, cpu.PC, 0);
			break;
		}
		cpu_interrupt((uint8_t)val);
		break;
	case OP_IRET:
		if (EXT_ON) {
			ext_iret();
			break;
		}
		addr = pop_word();
		cpu.flags = (uint8_t)pop_word();
		cpu.int_enabled = 1;
		VLOG("IRET to 0x%X flags=0x%X\n", addr, cpu.flags);
		cpu.PC = addr;
		break;
	case OP_CLI:
		cpu.int_enabled = 0;
		break;
	case OP_STI:
		cpu.int_enabled = 1;
		break;
	case OP_LOAD32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) cpu.registers[reg] = val32;
		break;
	case OP_STORE32:
		reg = fetch_byte();
		addr32 = fetch_dword();
		if (reg < 8) mem_write(addr32, cpu.registers[reg], 4);
		break;
	case OP_ADD32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint64_t a = cpu.registers[reg];
			uint64_t sum = a + (uint64_t)val32;
			cpu.flags = 0;
			if ((sum & 0xFFFFFFFF) == 0) cpu.flags |= FLAG_Z;
			if (sum > 0xFFFFFFFF) cpu.flags |= FLAG_C;
			if (sum & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = (uint32_t)sum;
		}
		break;
	case OP_SUB32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t diff = a - val32;
			cpu.flags = 0;
			if (diff == 0) cpu.flags |= FLAG_Z;
			if (val32 > a) cpu.flags |= FLAG_C;
			if (diff & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = diff;
		}
		break;
	case OP_MUL32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint64_t a = cpu.registers[reg];
			uint64_t prod = a * (uint64_t)val32;
			cpu.flags = 0;
			if ((prod & 0xFFFFFFFF) == 0) cpu.flags |= FLAG_Z;
			if (prod > 0xFFFFFFFF) cpu.flags |= FLAG_C | FLAG_V;
			if (prod & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = (uint32_t)prod;
		}
		break;
	case OP_DIV32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			if (val32 == 0) {
				VLOG("division by zero\n");
				cpu_interrupt(0);
			} else {
				uint32_t a = cpu.registers[reg];
				uint32_t quot = a / val32;
				cpu.flags = 0;
				if (quot == 0) cpu.flags |= FLAG_Z;
				if (quot & 0x80000000) cpu.flags |= FLAG_S;
				cpu.registers[reg] = quot;
			}
		}
		break;
	case OP_SHL32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t res = a << (val32 & 31);
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (val32 > 0 && val32 <= 32 && ((a << (val32 - 1)) & 0x80000000)) cpu.flags |= FLAG_C;
			if (res & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
		}
		break;
	case OP_SHR32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t res = a >> (val32 & 31);
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (val32 > 0 && val32 <= 32 && ((a >> (val32 - 1)) & 1)) cpu.flags |= FLAG_C;
			if (res & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
		}
		break;
	case OP_AND32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) cpu.registers[reg] &= val32;
		break;
	case OP_OR32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) cpu.registers[reg] |= val32;
		break;
	case OP_XOR32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) cpu.registers[reg] ^= val32;
		break;
	case OP_CMP32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t diff = a - val32;
			cpu.flags = 0;
			if (diff == 0) cpu.flags |= FLAG_Z;
			if (val32 > a) cpu.flags |= FLAG_C;
			if (diff & 0x80000000) cpu.flags |= FLAG_S;
		}
		break;
	case OP_MOV:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			cpu.registers[dst] = cpu.registers[src];
			VLOG("MOV: R%d = R%d (0x%X)\n", dst, src, cpu.registers[dst]);
		}
		break;
	case OP_ADD_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t a = cpu.registers[dst];
			uint32_t b = cpu.registers[src];
			uint32_t result = a + b;
			set_flags(a, b, result, 0);
			cpu.registers[dst] = result & 0xFFFF;
			VLOG("ADD_REG: R%d=0x%X + R%d=0x%X = 0x%X flags=0x%X\n", dst, a, src, b, cpu.registers[dst], cpu.flags);
		}
		break;
	case OP_SUB_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t a = cpu.registers[dst];
			uint32_t b = cpu.registers[src];
			uint32_t result = (a - b) & 0xFFFF;
			set_flags(a, b, result, 1);
			cpu.registers[dst] = result;
			VLOG("SUB_REG: R%d=0x%X - R%d=0x%X = 0x%X flags=0x%X\n", dst, a, src, b, result, cpu.flags);
		}
		break;
	case OP_CMP_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t a = cpu.registers[dst];
			uint32_t b = cpu.registers[src];
			uint32_t result = (a - b) & 0xFFFF;
			set_flags(a, b, result, 1);
			VLOG("CMP_REG: R%d=0x%X vs R%d=0x%X flags=0x%X\n", dst, a, src, b, cpu.flags);
		}
		break;
	case OP_AND_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) cpu.registers[dst] &= cpu.registers[src];
		break;
	case OP_OR_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) cpu.registers[dst] |= cpu.registers[src];
		break;
	case OP_XOR_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) cpu.registers[dst] ^= cpu.registers[src];
		break;
	case OP_MUL_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t a = cpu.registers[dst];
			uint32_t b = cpu.registers[src];
			uint32_t prod = a * b;
			uint32_t res = prod & 0xFFFF;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (prod > 0xFFFF) cpu.flags |= FLAG_C | FLAG_V;
			if (res & 0x8000) cpu.flags |= FLAG_S;
			cpu.registers[dst] = res;
		}
		break;
	case OP_DIV_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t b = cpu.registers[src];
			if (b == 0) {
				VLOG("division by zero\n");
				cpu_interrupt(0);
			} else {
				uint32_t a = cpu.registers[dst];
				uint32_t res = (a / b) & 0xFFFF;
				cpu.flags = 0;
				if (res == 0) cpu.flags |= FLAG_Z;
				if (res & 0x8000) cpu.flags |= FLAG_S;
				cpu.registers[dst] = res;
			}
		}
		break;
	case OP_PUSH:
		reg = fetch_byte();
		if (reg < 8) push_word((uint16_t)cpu.registers[reg]);
		break;
	case OP_POP:
		reg = fetch_byte();
		if (reg < 8) cpu.registers[reg] = pop_word();
		break;
	case OP_LOAD_INDIR:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		off = (int16_t)fetch_word();
		if (dst < 8 && src < 8) {
			eff = (uint32_t)((int32_t)cpu.registers[src] + off);
			cpu.registers[dst] = mem_read(eff, 2);
			VLOG("LOAD_INDIR: R%d = [R%d + %d] (0x%X = 0x%X)\n", dst, src, off, eff, cpu.registers[dst]);
		}
		break;
	case OP_STORE_INDIR:
		pair = fetch_byte();
		src = (pair >> 4) & 0x0F;
		reg = pair & 0x0F;
		off = (int16_t)fetch_word();
		if (src < 8 && reg < 8) {
			eff = (uint32_t)((int32_t)cpu.registers[src] + off);
			val = cpu.registers[reg];
			mem_write(eff, val, 2);
			VLOG("STORE_INDIR: [R%d + %d] (0x%X) = R%d (0x%X)\n", src, off, eff, reg, val);
		}
		break;
	case OP_LOAD32_INDIR:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		off = (int16_t)fetch_word();
		if (dst < 8 && src < 8) {
			eff = (uint32_t)((int32_t)cpu.registers[src] + off);
			cpu.registers[dst] = mem_read(eff, 4);
		}
		break;
	case OP_STORE32_INDIR:
		pair = fetch_byte();
		src = (pair >> 4) & 0x0F;
		reg = pair & 0x0F;
		off = (int16_t)fetch_word();
		if (src < 8 && reg < 8) {
			eff = (uint32_t)((int32_t)cpu.registers[src] + off);
			mem_write(eff, cpu.registers[reg], 4);
		}
		break;
	case OP_SYSCALL:
		if (EXT_ON) {
			cpu_deliver(VEC_SYSCALL, cpu.PC, 0);
			break;
		}
		cpu.mode = MODE_SUPERVISOR;
		cpu.syscall_pc = cpu.PC;
		addr = mem_read(IVT_BASE + 0x80, 2);
		VLOG("SYSCALL: user PC=0x%X jumping to 0x%X\n", cpu.syscall_pc, addr);
		cpu.PC = addr;
		break;
	case OP_SYSRET:
		if (EXT_ON) {
			ext_iret();
			break;
		}
		cpu.mode = MODE_USER;
		if (cpu.syscall_pc != 0) {
			cpu.PC = cpu.syscall_pc;
			cpu.syscall_pc = 0;
		}
		VLOG("SYSRET: to user PC=0x%X\n", cpu.PC);
		break;
	case OP_JMPR:
		pair = fetch_byte();
		src = pair & 15;
		if (src > 7) invalid_opcode(opcode);
		cpu.PC = cpu.registers[src];
		break;
	case OP_CALLR:
		pair = fetch_byte();
		src = pair & 15;
		if (src > 7) invalid_opcode(opcode);
		val32 = cpu.registers[src];
		push32(cpu.PC);
		cpu.PC = val32;
		break;
	case OP_JMPREL:
		fetch_byte();
		val32 = fetch_dword();
		cpu.PC += val32;
		break;
	case OP_CALLREL:
		fetch_byte();
		val32 = fetch_dword();
		push32(cpu.PC);
		cpu.PC += val32;
		break;
	case OP_BEQ: case OP_BNE: case OP_BLT: case OP_BGE: case OP_BLE: case OP_BGT:
	case OP_BLO: case OP_BHS: case OP_BLS: case OP_BHI: case OP_BMI: case OP_BPL:
	case OP_BVS: case OP_BVC:
		fetch_byte();
		val32 = fetch_dword();
		if (cond(opcode)) cpu.PC += val32;
		break;
	case OP_JMP32:
		fetch_byte();
		cpu.PC = fetch_dword();
		break;
	case OP_CALL32:
		fetch_byte();
		val32 = fetch_dword();
		push32(cpu.PC);
		cpu.PC = val32;
		break;
	case OP_ADC: case OP_SBB: case OP_SARV: case OP_SHLV: case OP_SHRV: case OP_ROLV:
	case OP_RORV: case OP_IMUL: case OP_IDIV: case OP_UMOD: case OP_IMOD: case OP_TEST:
	case OP_MULHU: case OP_MULHS: case OP_ADD32R: case OP_SUB32R: case OP_CMP32R:
	case OP_MUL32R: case OP_DIV32R:
		pair = fetch_byte();
		dst = pair >> 4;
		src = pair & 15;
		if (dst > 7 || src > 7) invalid_opcode(opcode);
		{
			uint32_t a = cpu.registers[dst], b = cpu.registers[src], r = 0;
			switch (opcode) {
			case OP_ADC: r = alu_add(a, b, (cpu.flags & FLAG_C) ? 1 : 0); break;
			case OP_SBB: r = alu_sub(a, b, (cpu.flags & FLAG_C) ? 1 : 0); break;
			case OP_SARV: r = alu_shift(2, a, b); break;
			case OP_SHLV: r = alu_shift(0, a, b); break;
			case OP_SHRV: r = alu_shift(1, a, b); break;
			case OP_ROLV: r = alu_shift(3, a, b); break;
			case OP_RORV: r = alu_shift(4, a, b); break;
			case OP_IMUL: r = alu_imul(a, b); break;
			case OP_IDIV: if (!alu_div(0, a, b, &r)) return; break;
			case OP_UMOD: if (!alu_div(1, a, b, &r)) return; break;
			case OP_IMOD: if (!alu_div(2, a, b, &r)) return; break;
			case OP_DIV32R: if (!alu_div(3, a, b, &r)) return; break;
			case OP_TEST: setzscv(a & b, 0, 0); return;
			case OP_MULHU: r = alu_mulh(a, b, 0); break;
			case OP_MULHS: r = alu_mulh(a, b, 1); break;
			case OP_ADD32R: r = alu_add(a, b, 0); break;
			case OP_SUB32R: r = alu_sub(a, b, 0); break;
			case OP_CMP32R: alu_sub(a, b, 0); return;
			default: {
				uint64_t p = (uint64_t)a * b;
				r = (uint32_t)p;
				setzscv(r, (p >> 32) != 0, (p >> 32) != 0);
				break;
			}
			}
			cpu.registers[dst] = r;
		}
		break;
	case OP_NEG:
		pair = fetch_byte();
		dst = pair >> 4;
		if (dst > 7) invalid_opcode(opcode);
		cpu.registers[dst] = alu_sub(0, cpu.registers[dst], 0);
		break;
	case OP_SAR: case OP_ROL: case OP_ROR: case OP_ADCI: case OP_SBBI: case OP_IMULI:
	case OP_IDIVI: case OP_UMODI: case OP_IMODI: case OP_TESTI:
		pair = fetch_byte();
		dst = pair >> 4;
		val32 = fetch_dword();
		if (dst > 7) invalid_opcode(opcode);
		{
			uint32_t a = cpu.registers[dst], r = 0;
			switch (opcode) {
			case OP_SAR: r = alu_shift(2, a, val32); break;
			case OP_ROL: r = alu_shift(3, a, val32); break;
			case OP_ROR: r = alu_shift(4, a, val32); break;
			case OP_ADCI: r = alu_add(a, val32, (cpu.flags & FLAG_C) ? 1 : 0); break;
			case OP_SBBI: r = alu_sub(a, val32, (cpu.flags & FLAG_C) ? 1 : 0); break;
			case OP_IMULI: r = alu_imul(a, val32); break;
			case OP_IDIVI: if (!alu_div(0, a, val32, &r)) return; break;
			case OP_UMODI: if (!alu_div(1, a, val32, &r)) return; break;
			case OP_IMODI: if (!alu_div(2, a, val32, &r)) return; break;
			default: setzscv(a & val32, 0, 0); return;
			}
			cpu.registers[dst] = r;
		}
		break;
	case OP_LDB: case OP_LDBS: case OP_LDH: case OP_LDHS: case OP_LDW: case OP_LEA:
		pair = fetch_byte();
		dst = pair >> 4;
		src = pair & 15;
		val32 = fetch_dword();
		if (dst > 7 || src > 8) invalid_opcode(opcode);
		eff = (src == 8 ? cpu.SP : cpu.registers[src]) + val32;
		switch (opcode) {
		case OP_LDB: val = mem_read(eff, 1); break;
		case OP_LDBS: val = (uint32_t)(int32_t)(int8_t)mem_read(eff, 1); break;
		case OP_LDH: val = mem_read(eff, 2); break;
		case OP_LDHS: val = (uint32_t)(int32_t)(int16_t)mem_read(eff, 2); break;
		case OP_LDW: val = mem_read(eff, 4); break;
		default: val = eff; break;
		}
		cpu.registers[dst] = val;
		break;
	case OP_STB: case OP_STH: case OP_STW:
		pair = fetch_byte();
		src = pair >> 4;
		dst = pair & 15;
		val32 = fetch_dword();
		if (src > 7 || dst > 8) invalid_opcode(opcode);
		eff = (dst == 8 ? cpu.SP : cpu.registers[dst]) + val32;
		mem_write(eff, cpu.registers[src], opcode == OP_STB ? 1 : opcode == OP_STH ? 2 : 4);
		break;
	case OP_LEAPC:
		pair = fetch_byte();
		dst = pair >> 4;
		val32 = fetch_dword();
		if (dst > 7) invalid_opcode(opcode);
		cpu.registers[dst] = cpu.PC + val32;
		break;
	case OP_MOVSP:
		pair = fetch_byte();
		dst = pair >> 4;
		if (dst > 7) invalid_opcode(opcode);
		cpu.registers[dst] = cpu.SP;
		break;
	case OP_SETSP:
		pair = fetch_byte();
		src = pair & 15;
		if (src > 7) invalid_opcode(opcode);
		cpu.SP = cpu.registers[src];
		break;
	case OP_ADDSP:
		fetch_byte();
		cpu.SP += fetch_dword();
		break;
	case OP_BRK:
		trap(VEC_BRK);
		break;
	case OP_MFCR:
		pair = fetch_byte();
		dst = pair >> 4;
		src = pair & 15;
		if (dst > 7 || src >= NUM_CR) invalid_opcode(opcode);
		priv();
		cpu.registers[dst] = cpu.cr[src];
		break;
	case OP_MTCR:
		pair = fetch_byte();
		dst = pair >> 4;
		src = pair & 15;
		if (src > 7 || dst >= NUM_CR) invalid_opcode(opcode);
		priv();
		cr_write(dst, cpu.registers[src]);
		break;
	case OP_TLBFLUSH:
		priv();
		cpu_flush_tlb();
		break;
	case OP_INVLPG:
		pair = fetch_byte();
		src = pair & 15;
		if (src > 7) invalid_opcode(opcode);
		priv();
		cpu_invlpg(cpu.registers[src]);
		break;
	case OP_WFI:
		priv();
		cpu_wfi();
		break;
	case OP_CPUID:
		pair = fetch_byte();
		dst = pair >> 4;
		if (dst > 7) invalid_opcode(opcode);
		cpu.registers[dst] = 2;
		break;
	case OP_PUSHF:
		push32(cpu_get_flags());
		break;
	case OP_POPF:
		val32 = pop32();
		if (cpu.mode == MODE_USER) val32 = (val32 & ~(uint32_t)(FLAG_I | FLAG_U)) | (cpu_get_flags() & (FLAG_I | FLAG_U));
		cpu_set_flags(val32);
		break;
	case OP_RDCYC:
		pair = fetch_byte();
		dst = pair >> 4;
		if (dst > 7) invalid_opcode(opcode);
		cpu.registers[dst] = (uint32_t)cpu.icount;
		break;
	case OP_RET32:
		cpu.PC = pop32();
		break;
	case OP_PUSH32:
		pair = fetch_byte();
		src = pair & 15;
		if (src > 7) invalid_opcode(opcode);
		push32(cpu.registers[src]);
		break;
	case OP_POP32:
		pair = fetch_byte();
		dst = pair >> 4;
		if (dst > 7) invalid_opcode(opcode);
		cpu.registers[dst] = pop32();
		break;
	case OP_HALT:
		cpu.halted = 1;
		break;
	default:
		invalid_opcode(opcode);
		break;
	}
}

static void step_one(void) {
	if (cpu.icount >= dev_next) {
		dev_events();
		if (cpu.halted) return;
	}
	if ((cpu.cr[CR_CTRL] & CR0_EXT) && cpu.int_enabled && (pic_pending & pic_mask)) {
		uint32_t p = pic_pending & pic_mask, n = 0;
		while (!(p & 1)) {
			p >>= 1;
			n++;
		}
		cpu.inst_pc = cpu.PC;
		cpu_deliver(VEC_IRQ + n, cpu.PC, 0);
	}
	{
		uint32_t pc = cpu.PC;
		static DEnt cur;
		DEnt tmp, *e;
		cpu.inst_pc = pc;
		if (dc_on && (pc & ~0xFFFu) == f_tag && f_ppn >= 0 && (pc & 0xFFF) <= 0xFFA) {
			DEnt *t = dc_tab[f_ppn];
			if (!t) {
				t = calloc(4096, sizeof(DEnt));
				if (!t) {
					fprintf(stderr, "out of memory\n");
					exit(EXIT_FATAL);
				}
				dc_tab[f_ppn] = t;
			}
			e = &t[pc & 0xFFF];
			if (!e->len) {
				decode(pc, &tmp);
				if ((pc & 0xFFF) + tmp.len <= 0x1000) *e = tmp;
				else e = &tmp;
			}
		} else {
			decode(pc, &tmp);
			e = &tmp;
		}
		cur = *e;
		ib = cur.b;
		ipos = 0;
		if (trace_f) trace_begin();
		cpu.PC = pc + cur.len;
		execute_instruction();
	}
	cpu.icount++;
}

void cpu_run(void) {
	jb_set = 1;
	setjmp(fault_jb);
	while (!cpu.halted) step_one();
	jb_set = 0;
	cpu_trace_end();
	dev_flush();
}

void cpu_step(void) {
	jb_set = 1;
	if (setjmp(fault_jb) == 0) step_one();
	jb_set = 0;
	dev_flush();
}

void cpu_wfi(void) {
	dev_wfi();
}

static int tr_pending;
static uint32_t tr_regs[8], tr_sp, tr_pc, tr_flags;
static uint64_t tr_count;
static char tr_dis[64];

static void trace_flush(void) {
	uint32_t f = cpu_get_flags();
	if (!tr_pending) return;
	fprintf(trace_f, "%llu %08X %-28s", (unsigned long long)tr_count, tr_pc, tr_dis);
	for (int i = 0; i < 8; i++)
		if (cpu.registers[i] != tr_regs[i]) fprintf(trace_f, " R%d=%08X", i, cpu.registers[i]);
	if (cpu.SP != tr_sp) fprintf(trace_f, " SP=%08X", cpu.SP);
	if (f != tr_flags) fprintf(trace_f, " FLAGS=%02X", f);
	fputc('\n', trace_f);
	tr_pending = 0;
}

static void trace_begin(void) {
	trace_flush();
	memcpy(tr_regs, cpu.registers, sizeof(tr_regs));
	tr_sp = cpu.SP;
	tr_pc = cpu.PC;
	tr_flags = cpu_get_flags();
	tr_count = cpu.icount;
	cpu_disassemble(cpu.PC, tr_dis, sizeof(tr_dis));
	tr_pending = 1;
}

void cpu_set_trace(FILE *f) {
	trace_f = f;
	tr_pending = 0;
}

void cpu_trace_end(void) {
	if (trace_f) {
		trace_flush();
		fflush(trace_f);
	}
}

int mem_poke(uint32_t va, uint8_t b) {
	uint8_t old;
	uint32_t pa = va;
	if (!mem_peek(va, &old)) return 0;
	if (xl_active() && (cpu.cr[CR_CTRL] & CR0_PG)) {
		uint32_t pde = rd32(cpu.memory + (cpu.cr[CR_PTBASE] & ~0xFFFu) + (va >> 22) * 4);
		uint32_t pte = rd32(cpu.memory + (pde & ~0xFFFu) + ((va >> 12) & 0x3FF) * 4);
		pa = (pte & ~0xFFFu) | (va & 0xFFF);
	} else if (xl_active() && cpu.mode == MODE_USER) {
		pa = cpu.cr[CR_BASE] + va;
	}
	if (pa >= cpu.ram_size) return 0;
	cpu.memory[pa] = b;
	cpu_mem_written(pa, 1);
	return 1;
}

static void put32(FILE *f, uint32_t v) {
	uint8_t b[4];
	wr32(b, v);
	fwrite(b, 1, 4, f);
}

static int get32(FILE *f, uint32_t *v) {
	uint8_t b[4];
	if (fread(b, 1, 4, f) != 4) return -1;
	*v = rd32(b);
	return 0;
}

int cpu_save(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	fwrite("TST1", 1, 4, f);
	put32(f, cpu.ram_size);
	put32(f, cpu.PC);
	for (int i = 0; i < 8; i++) put32(f, cpu.registers[i]);
	put32(f, cpu.SP);
	put32(f, cpu.flags);
	put32(f, cpu.int_enabled);
	put32(f, cpu.mode);
	put32(f, cpu.halted);
	put32(f, cpu.syscall_pc);
	put32(f, cpu.disk_sector);
	put32(f, cpu.disk_buffer);
	for (int i = 0; i < 16; i++) put32(f, cpu.cr[i]);
	put32(f, (uint32_t)cpu.icount);
	put32(f, (uint32_t)(cpu.icount >> 32));
	fwrite(cpu.memory, 1, cpu.ram_size, f);
	dev_save(f);
	if (fclose(f)) return -1;
	return 0;
}

int cpu_load(const char *path) {
	FILE *f = fopen(path, "rb");
	uint8_t magic[4];
	uint32_t v[40], ram, lo, hi;
	int i;
	if (!f) return -1;
	if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "TST1", 4) || get32(f, &ram) || ram == 0 || ram > (MAX_RAM_MB << 20)) goto bad;
	for (i = 0; i < 33; i++)
		if (get32(f, &v[i])) goto bad;
	if (get32(f, &lo) || get32(f, &hi)) goto bad;
	cpu_init_size(ram);
	cpu.PC = v[0];
	for (i = 0; i < 8; i++) cpu.registers[i] = v[1 + i];
	cpu.SP = v[9];
	cpu.flags = (uint8_t)v[10];
	cpu.int_enabled = (uint8_t)v[11];
	cpu.mode = (uint8_t)v[12];
	cpu.halted = (uint8_t)v[13];
	cpu.syscall_pc = v[14];
	cpu.disk_sector = v[15];
	cpu.disk_buffer = v[16];
	for (i = 0; i < 16; i++) cpu.cr[i] = v[17 + i];
	cpu.cr[CR_RAMSIZE] = ram;
	cpu.icount = ((uint64_t)hi << 32) | lo;
	if (fread(cpu.memory, 1, ram, f) != ram || dev_load(f)) goto bad;
	fclose(f);
	cpu_flush_tlb();
	cpu_dc_flush();
	dev_next = 0;
	return 0;
bad:
	fclose(f);
	return -1;
}
