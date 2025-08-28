/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_PMAP_H
#define JOS_KERN_PMAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/memlayout.h>
#include <inc/assert.h>

extern char bootstacktop[], bootstack[];

extern struct PageInfo *pages;
extern size_t npages;

extern pml4e_t *kern_pml4;


/* This macro takes a kernel virtual address -- an address that points above
 * KERNBASE, where the machine's maximum 256MB of physical memory is mapped --
 * and returns the corresponding physical address.  It panics if you pass it a
 * non-kernel virtual address.
 */
#define PADDR(kva) _paddr(__FILE__, __LINE__, kva)

static inline physaddr_t
_paddr(const char *file, int line, void *kva)
{
	if ((uint64_t)kva < KERNBASE)
		_panic(file, line, "PADDR called with invalid kva %08lx", kva);
	return (physaddr_t)kva - KERNBASE;
}

/* This macro takes a physical address and returns the corresponding kernel
 * virtual address.  It panics if you pass an invalid physical address. */
#define KADDR(pa) _kaddr(__FILE__, __LINE__, pa)

static inline void*
_kaddr(const char *file, int line, physaddr_t pa)
{
	if (PGNUM(pa) >= npages)
		_panic(file, line, "KADDR called with invalid pa %08lx", pa);
	return (void *)(pa + KERNBASE);
}


enum {
	// For page_alloc, zero the returned physical page.
	ALLOC_ZERO = 1<<0,
};

void	mem_init(void);

void	page_init(void);
struct PageInfo *page_alloc(int alloc_flags);
void	page_free(struct PageInfo *pp);
int	page_insert(pml4e_t *pml4e, struct PageInfo *pp, void *va, int perm);
void	page_remove(pml4e_t *pml4e, void *va);
struct PageInfo *page_lookup(pml4e_t *pml4e, void *va, pte_t **pte_store);
void	page_decref(struct PageInfo *pp);

void	tlb_invalidate(pml4e_t *pml4e, void *va);

static inline physaddr_t
page2pa(struct PageInfo *pp)
{
	return (pp - pages) << PGSHIFT;
}

static inline struct PageInfo*
pa2page(physaddr_t pa)
{
	if (PGNUM(pa) >= npages)
		panic("pa2page called with invalid pa");
	return &pages[PGNUM(pa)];
}

static inline void*
page2kva(struct PageInfo *pp)
{
	return KADDR(page2pa(pp));
}
pte_t *pml4e_walk(pml4e_t *pml4e, const void *va, int create);
pte_t *pdpe_walk(pdpe_t *pdpe,const void *va,int create);
pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create);

#endif /* !JOS_KERN_PMAP_H */
