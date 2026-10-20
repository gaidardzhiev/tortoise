#include "kernel.h"

extern char _end[];

static unsigned char low_used[LOWMEM / PAGE];
static unsigned low_first;
static unsigned fbits[65536 / 32];
static unsigned short fref[65536];
static unsigned nframes;
static unsigned nfree;
static unsigned fhint;
static unsigned *id_table;
static unsigned *dev_table;
static unsigned *kmap_table;
unsigned kernel_pgdir;

unsigned low_alloc_n(int n) {
	unsigned i, k;
	for (i = low_first; i + n <= LOWMEM / PAGE; i++) {
		for (k = 0; k < (unsigned)n && !low_used[i + k]; k++) ;
		if (k == (unsigned)n) {
			for (k = 0; k < (unsigned)n; k++) low_used[i + k] = 1;
			memset((void *)(i * PAGE), 0, (unsigned)n * PAGE);
			return i * PAGE;
		}
		i += k;
	}
	return 0;
}

unsigned low_alloc(void) {
	return low_alloc_n(1);
}

void low_free_n(unsigned pa, int n) {
	unsigned i = pa / PAGE, k;
	for (k = 0; k < (unsigned)n; k++) {
		if (i + k < low_first || i + k >= LOWMEM / PAGE || !low_used[i + k]) panic("low_free: bad page");
		low_used[i + k] = 0;
	}
}

void low_free(unsigned pa) {
	low_free_n(pa, 1);
}

unsigned low_free_pages(void) {
	unsigned i, n = 0;
	for (i = low_first; i < LOWMEM / PAGE; i++)
		if (!low_used[i]) n++;
	return n;
}

unsigned low_total_pages(void) {
	return LOWMEM / PAGE - low_first;
}

unsigned frame_alloc(void) {
	unsigned i, n;
	for (n = 0; n < nframes; n++) {
		i = (fhint + n) % nframes;
		if (!(fbits[i / 32] & (1u << (i % 32)))) {
			fbits[i / 32] |= 1u << (i % 32);
			fref[i] = 1;
			fhint = i + 1;
			nfree--;
			return LOWMEM + i * PAGE;
		}
	}
	return 0;
}

unsigned frame_alloc_contig(unsigned n) {
	unsigned i, k;
	for (i = 0; i + n <= nframes; i++) {
		for (k = 0; k < n && !(fbits[(i + k) / 32] & (1u << ((i + k) % 32))); k++) ;
		if (k == n) {
			for (k = 0; k < n; k++) {
				fbits[(i + k) / 32] |= 1u << ((i + k) % 32);
				fref[i + k] = 1;
				memset(kmap(0, LOWMEM + (i + k) * PAGE), 0, PAGE);
			}
			nfree -= n;
			return LOWMEM + i * PAGE;
		}
		i += k;
	}
	return 0;
}

static unsigned fidx(unsigned pa) {
	unsigned i = (pa - LOWMEM) / PAGE;
	if (pa < LOWMEM || i >= nframes || !(fbits[i / 32] & (1u << (i % 32)))) panic("bad frame");
	return i;
}

void frame_free(unsigned pa) {
	unsigned i = fidx(pa);
	if (--fref[i] == 0) {
		fbits[i / 32] &= ~(1u << (i % 32));
		nfree++;
	}
}

void frame_ref(unsigned pa) {
	fref[fidx(pa)]++;
}

unsigned frame_count(unsigned pa) {
	return fref[fidx(pa)];
}

unsigned frames_free(void) {
	return nfree;
}

unsigned frames_total(void) {
	return nframes;
}

unsigned *kmap(int slot, unsigned pa) {
	unsigned va = KMAP_BASE + (unsigned)slot * PAGE;
	kmap_table[slot] = (pa & ~0xFFFu) | PTE_P | PTE_W;
	invlpg(va);
	return (unsigned *)va;
}

void mm_init(void) {
	unsigned i;
	low_first = (((unsigned)_end + PAGE - 1) & ~(PAGE - 1)) / PAGE;
	for (i = 0; i < low_first; i++) low_used[i] = 1;
	nframes = ram_size > LOWMEM ? (ram_size - LOWMEM) / PAGE : 0;
	if (nframes > 65536) nframes = 65536;
	nfree = nframes;
	id_table = (unsigned *)low_alloc();
	dev_table = (unsigned *)low_alloc();
	kmap_table = (unsigned *)low_alloc();
	for (i = 0; i < 1024; i++) id_table[i] = i * PAGE | PTE_P | PTE_W | PTE_X;
	for (i = 0; i < 16; i++) dev_table[i] = (DEV_VBASE + i * PAGE) | PTE_P | PTE_W;
	kernel_pgdir = new_pgdir();
}

unsigned new_pgdir(void) {
	unsigned *pd = (unsigned *)low_alloc();
	if (!pd) return 0;
	pd[0] = (unsigned)id_table | PTE_P;
	pd[KMAP_BASE >> 22] = (unsigned)kmap_table | PTE_P;
	pd[DEV_VBASE >> 22] = (unsigned)dev_table | PTE_P;
	return (unsigned)pd;
}

static int shared_pde(unsigned i) {
	return i == 0 || i == (KMAP_BASE >> 22) || i == (DEV_VBASE >> 22);
}

unsigned walk(unsigned pgdir, unsigned va) {
	unsigned *pd = (unsigned *)pgdir;
	unsigned pde = pd[va >> 22];
	if (!(pde & PTE_P)) return 0;
	return ((unsigned *)(pde & ~0xFFFu))[(va >> 12) & 0x3FF];
}

static unsigned *pte_slot(unsigned pgdir, unsigned va, int create) {
	unsigned *pd = (unsigned *)pgdir;
	unsigned pde = pd[va >> 22];
	if (!(pde & PTE_P)) {
		unsigned t;
		if (!create) return NULL;
		t = low_alloc();
		if (!t) return NULL;
		pde = t | PTE_P;
		pd[va >> 22] = pde;
	}
	return &((unsigned *)(pde & ~0xFFFu))[(va >> 12) & 0x3FF];
}

int map_page(unsigned pgdir, unsigned va, unsigned pa, unsigned flags) {
	unsigned *s = pte_slot(pgdir, va, 1);
	if (!s) return -ENOMEM;
	*s = (pa & ~0xFFFu) | flags | PTE_P;
	return 0;
}

static void unmap_page(unsigned pgdir, unsigned va) {
	unsigned *s = pte_slot(pgdir, va, 0);
	if (s) *s = 0;
}

static void flush_if_current(unsigned pgdir, unsigned va) {
	if (pgdir == get_cr(3)) invlpg(va);
}

int alloc_user(unsigned pgdir, unsigned lo, unsigned hi, unsigned flags) {
	unsigned va;
	for (va = lo & ~0xFFFu; va < hi; va += PAGE) {
		unsigned pa;
		if (walk(pgdir, va) & PTE_P) continue;
		pa = frame_alloc();
		if (!pa) return -ENOMEM;
		memset(kmap(0, pa), 0, PAGE);
		if (map_page(pgdir, va, pa, flags)) {
			frame_free(pa);
			return -ENOMEM;
		}
		flush_if_current(pgdir, va);
	}
	return 0;
}

void free_user(unsigned pgdir, unsigned lo, unsigned hi) {
	unsigned va;
	for (va = lo & ~0xFFFu; va < hi; va += PAGE) {
		unsigned pte = walk(pgdir, va);
		if (pte & PTE_P) {
			frame_free(pte & ~0xFFFu);
			unmap_page(pgdir, va);
			flush_if_current(pgdir, va);
		}
	}
}

void free_pgdir(unsigned pgdir) {
	unsigned *pd = (unsigned *)pgdir;
	unsigned i, j;
	for (i = 0; i < 1024; i++) {
		unsigned *pt;
		if (shared_pde(i) || !(pd[i] & PTE_P)) continue;
		pt = (unsigned *)(pd[i] & ~0xFFFu);
		for (j = 0; j < 1024; j++)
			if (pt[j] & PTE_P) frame_free(pt[j] & ~0xFFFu);
		low_free((unsigned)pt);
	}
	low_free(pgdir);
}

Region *find_region(Proc *p, unsigned va) {
	Region *r;
	for (r = p->regions; r; r = r->next)
		if (va >= r->start && va < r->end) return r;
	return NULL;
}

int add_region(Proc *p, unsigned start, unsigned end, unsigned prot, unsigned flags, unsigned kind) {
	Region *r = kmalloc(sizeof(Region)), **pp = &p->regions;
	if (!r) return -ENOMEM;
	r->start = start;
	r->end = end;
	r->prot = prot;
	r->flags = flags;
	r->kind = kind;
	while (*pp && (*pp)->start < start) pp = &(*pp)->next;
	r->next = *pp;
	*pp = r;
	return 0;
}

void free_regions(Region *r) {
	while (r) {
		Region *n = r->next;
		kfree(r);
		r = n;
	}
}

Region *copy_regions(Region *r) {
	Region head, *cur = &head;
	head.next = NULL;
	for (; r; r = r->next) {
		Region *c = kmalloc(sizeof(Region));
		if (!c) {
			free_regions(head.next);
			return (Region *)-1;
		}
		*c = *r;
		c->next = NULL;
		cur->next = c;
		cur = c;
	}
	return head.next;
}

unsigned region_flags(Region *r) {
	return PTE_U | ((r->prot & PROT_WRITE) ? PTE_W : 0) | ((r->prot & PROT_EXEC) ? PTE_X : 0);
}

int fault_in(Proc *p, unsigned va, int write) {
	Region *r = find_region(p, va);
	unsigned pte, page = va & ~0xFFFu;
	if (!r) return -1;
	if (write && !(r->prot & PROT_WRITE)) return -1;
	pte = walk(p->pgdir, page);
	if (!(pte & PTE_P)) {
		unsigned pa = frame_alloc();
		if (!pa) return -1;
		memset(kmap(0, pa), 0, PAGE);
		if (map_page(p->pgdir, page, pa, region_flags(r))) {
			frame_free(pa);
			return -1;
		}
		flush_if_current(p->pgdir, page);
		return 0;
	}
	if (write && !(pte & PTE_W)) {
		unsigned pa = pte & ~0xFFFu;
		if (!(pte & PTE_COW)) return -1;
		if (frame_count(pa) > 1) {
			unsigned np = frame_alloc();
			if (!np) return -1;
			memcpy(kmap(1, np), kmap(0, pa), PAGE);
			frame_free(pa);
			map_page(p->pgdir, page, np, region_flags(r));
		} else {
			map_page(p->pgdir, page, pa, region_flags(r));
		}
		flush_if_current(p->pgdir, page);
	}
	return 0;
}

int fork_space(Proc *src, Proc *dst) {
	unsigned *pd = (unsigned *)src->pgdir;
	unsigned i, j;
	for (i = 0; i < 1024; i++) {
		unsigned *pt;
		if (shared_pde(i) || !(pd[i] & PTE_P)) continue;
		pt = (unsigned *)(pd[i] & ~0xFFFu);
		for (j = 0; j < 1024; j++) {
			unsigned va = (i << 22) | (j << 12), pte = pt[j];
			Region *r;
			if (!(pte & PTE_P)) continue;
			r = find_region(src, va);
			if (r && !(r->flags & MAP_SHARED) && ((pte & PTE_W) || (pte & PTE_COW))) {
				pte = (pte & ~(unsigned)PTE_W) | PTE_COW;
				pt[j] = pte;
			}
			if (map_page(dst->pgdir, va, pte & ~0xFFFu, pte & 0xFFF & ~(unsigned)PTE_P)) return -ENOMEM;
			frame_ref(pte & ~0xFFFu);
		}
	}
	tlbflush();
	return 0;
}

static void unmap_range(Proc *p, unsigned lo, unsigned hi) {
	Region **pp = &p->regions;
	free_user(p->pgdir, lo, hi);
	while (*pp) {
		Region *r = *pp;
		if (r->end <= lo || r->start >= hi) {
			pp = &r->next;
			continue;
		}
		if (r->start >= lo && r->end <= hi) {
			*pp = r->next;
			kfree(r);
			continue;
		}
		if (r->start < lo && r->end > hi) {
			Region *n = kmalloc(sizeof(Region));
			if (n) {
				*n = *r;
				n->start = hi;
				r->next = n;
			}
			r->end = lo;
			pp = &r->next;
			continue;
		}
		if (r->start < lo) r->end = lo;
		else r->start = hi;
		pp = &r->next;
	}
}

int do_mmap(unsigned addr, unsigned len, unsigned prot, unsigned flags, int fd) {
	unsigned share = flags & (MAP_SHARED | MAP_PRIVATE), start;
	Proc *p = curproc;
	if (fd != -1 || !(flags & MAP_ANONYMOUS) || (share != MAP_SHARED && share != MAP_PRIVATE) || len == 0 || len > 0x40000000u) return -EINVAL;
	len = (len + PAGE - 1) & ~(PAGE - 1);
	if (flags & MAP_FIXED) {
		if ((addr & 0xFFF) || addr < USER_BASE || addr + len < addr || addr + len > USER_STACK_TOP - USER_STACK_MAX) return -EINVAL;
		unmap_range(p, addr, addr + len);
		start = addr;
	} else {
		start = MMAP_TOP - len;
		while (1) {
			Region *r;
			int moved = 0;
			if (start < p->brk || start < USER_BASE) return -ENOMEM;
			for (r = p->regions; r; r = r->next)
				if (r->start < start + len && r->end > start) {
					start = (r->start - len) & ~(PAGE - 1);
					moved = 1;
					break;
				}
			if (!moved) break;
		}
	}
	if (add_region(p, start, start + len, prot & 7, flags & (MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS), R_MMAP)) return -ENOMEM;
	if (flags & MAP_SHARED) {
		Region *r = find_region(p, start);
		if (alloc_user(p->pgdir, start, start + len, region_flags(r))) {
			unmap_range(p, start, start + len);
			return -ENOMEM;
		}
	}
	return (int)start;
}

int do_munmap(unsigned addr, unsigned len) {
	if ((addr & 0xFFF) || len == 0 || addr < USER_BASE || addr + len < addr) return -EINVAL;
	len = (len + PAGE - 1) & ~(PAGE - 1);
	unmap_range(curproc, addr, addr + len);
	return 0;
}

int user_ok(unsigned va, unsigned n, int write) {
	unsigned p, end;
	if (!curproc) return 0;
	if (n == 0) return va >= USER_BASE;
	end = va + n;
	if (va < USER_BASE || end < va || end > KMAP_BASE) return 0;
	for (p = va & ~0xFFFu; p < end; p += PAGE) {
		unsigned pte = walk(curproc->pgdir, p);
		if (!(pte & PTE_P) || !(pte & PTE_U) || (write && !(pte & PTE_W))) {
			if (fault_in(curproc, p, write)) return 0;
			pte = walk(curproc->pgdir, p);
			if (!(pte & PTE_U)) return 0;
		}
	}
	return 1;
}

int copyin(void *dst, unsigned src, unsigned n) {
	if (!user_ok(src, n, 0)) return -EFAULT;
	memcpy(dst, (void *)src, n);
	return 0;
}

int copyout(unsigned dst, const void *src, unsigned n) {
	if (!user_ok(dst, n, 1)) return -EFAULT;
	memcpy((void *)dst, src, n);
	return 0;
}

int copyinstr(char *dst, unsigned src, unsigned max) {
	unsigned i;
	for (i = 0; i < max; i++) {
		if (((src + i) & 0xFFF) == 0 || i == 0) {
			if (!user_ok(src + i, 1, 0)) return -EFAULT;
		}
		dst[i] = *(char *)(src + i);
		if (!dst[i]) return (int)i;
	}
	return -EINVAL;
}

int copy_to_space(unsigned pgdir, unsigned va, const void *src, unsigned n) {
	const char *s = src;
	while (n) {
		unsigned pte = walk(pgdir, va), off = va & 0xFFF, k = PAGE - off;
		char *p;
		if (!(pte & PTE_P)) return -EFAULT;
		if (k > n) k = n;
		p = (char *)kmap(2, pte);
		memcpy(p + off, s, k);
		s += k;
		va += k;
		n -= k;
	}
	return 0;
}
