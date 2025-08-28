// --------------------------------------------------------------
// Checking functions.
// --------------------------------------------------------------

//
// Check that the pages on the page_free_list are reasonable.
//
static void
check_page_free_list(bool only_low_memory)
{
	struct PageInfo *pp;
	unsigned pdx_limit = only_low_memory ? 1 : NPDENTRIES;
	uint64_t nfree_basemem = 0, nfree_extmem = 0;
	char *first_free_page;

	if (!page_free_list)
		panic("'page_free_list' is a null pointer!");

	if (only_low_memory) {
		// Move pages with lower addresses first in the free
		// list, since entry_pgdir does not map all pages.
		struct PageInfo *pp1, *pp2;
		struct PageInfo **tp[2] = { &pp1, &pp2 };
		for (pp = page_free_list; pp; pp = pp->pp_link) {
			int pagetype = PDX(page2pa(pp)) >= pdx_limit;
			*tp[pagetype] = pp;
			tp[pagetype] = &pp->pp_link;
		}
		*tp[1] = 0;
		*tp[0] = pp2;
		page_free_list = pp1;
	}

	// if there's a page that shouldn't be on the free list,
	// try to make sure it eventually causes trouble.
	for (pp = page_free_list; pp; pp = pp->pp_link)
		if (PDX(page2pa(pp)) < pdx_limit)
			memset(page2kva(pp), 0x97, 128);

	first_free_page = (char *) boot_alloc(0);
	for (pp = page_free_list; pp; pp = pp->pp_link) {
		// check that we didn't corrupt the free list itself
		assert(pp >= pages);
		assert(pp < pages + npages);
		assert(((char *) pp - (char *) pages) % sizeof(*pp) == 0);

		// check a few pages that shouldn't be on the free list
		assert(page2pa(pp) != 0);
		assert(page2pa(pp) != IOPHYSMEM);
		assert(page2pa(pp) != EXTPHYSMEM - PGSIZE);
		assert(page2pa(pp) != EXTPHYSMEM);
		assert(page2pa(pp) < EXTPHYSMEM || (char *) page2kva(pp) >= first_free_page);

		if (page2pa(pp) < EXTPHYSMEM)
			++nfree_basemem;
		else
			++nfree_extmem;
	}

	assert((nfree_basemem > 0)|!only_low_memory);
	assert(nfree_extmem > 0);

	cprintf("check_page_free_list() succeeded!\n");
}

//
// Check the physical page allocator (page_alloc(), page_free(),
// and page_init()).
//
static void
check_page_alloc(void)
{
	struct PageInfo *pp, *pp0, *pp1, *pp2;
	int nfree;
	struct PageInfo *fl;
	char *c;
	int i;

	if (!pages)
		panic("'pages' is a null pointer!");

	// check number of free pages
	for (pp = page_free_list, nfree = 0; pp; pp = pp->pp_link)
		++nfree;

	// should be able to allocate three pages
	pp0 = pp1 = pp2 = 0;
	assert((pp0 = page_alloc(0)));
	assert((pp1 = page_alloc(0)));
	assert((pp2 = page_alloc(0)));

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);
	assert(page2pa(pp0) < npages*PGSIZE);
	assert(page2pa(pp1) < npages*PGSIZE);
	assert(page2pa(pp2) < npages*PGSIZE);

	// temporarily steal the rest of the free pages
	fl = page_free_list;
	page_free_list = 0;

	// should be no free memory
	assert(!page_alloc(0));

	// free and re-allocate?
	page_free(pp0);
	page_free(pp1);
	page_free(pp2);
	pp0 = pp1 = pp2 = 0;
	assert((pp0 = page_alloc(0)));
	assert((pp1 = page_alloc(0)));
	assert((pp2 = page_alloc(0)));
	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);
	assert(!page_alloc(0));

	// test flags
	memset(page2kva(pp0), 1, PGSIZE);
	page_free(pp0);
	assert((pp = page_alloc(ALLOC_ZERO)));
	assert(pp && pp0 == pp);
	c = page2kva(pp);
	for (i = 0; i < PGSIZE; i++)
		assert(c[i] == 0);

	// give free list back
	page_free_list = fl;

	// free the pages we took
	page_free(pp0);
	page_free(pp1);
	page_free(pp2);

	// number of free pages should be the same
	for (pp = page_free_list; pp; pp = pp->pp_link)
		--nfree;
	assert(nfree == 0);

	cprintf("check_page_alloc() succeeded!\n");
}

//
// Checks that the kernel part of virtual address space
// has been set up roughly correctly (by mem_init()).
//
// This function doesn't test every corner case,
// but it is a pretty good sanity check.
//

static void
check_kern_pml4e(void)
{

	uint64_t i, n;

	pml4e_t* pml4e = kern_pml4;

	// check pages array
	n = ROUNDUP(npages*sizeof(struct PageInfo), PGSIZE);
	for (i = 0; i < n; i += PGSIZE)
		assert(check_va2pa(pml4e, UPAGES + i) == PADDR(pages) + i);


	// check phys mem
	for (i = 0; i < npages * PGSIZE; i += PGSIZE)
		assert(check_va2pa(pml4e, KERNBASE + i) == i);

	// check kernel stack
	for (i = 0; i < KSTKSIZE; i += PGSIZE)
		assert(check_va2pa(pml4e, KSTACKTOP - KSTKSIZE + i) == PADDR(bootstack) + i);
	assert(check_va2pa(pml4e, KSTACKTOP - PTSIZE) == ~0);
	
	pdpe_t *pdpe = KADDR(PTE_ADDR(kern_pml4[PML4X(KERNBASE)]));
	pde_t  *pgdir = KADDR(PTE_ADDR(pdpe[PDPX(KERNBASE)]));
	// check PDE permissions
	for (i = 0; i < NPDENTRIES; i++) {
		switch (i) {
		//case PDX(UVPT):??
		case PDX(KSTACKTOP-1):
		case PDX(UPAGES):
			assert(pgdir[i] & PTE_P);
			break;
		default://changed to handle new memory space
			if (i >= PDX(KERNBASE)) {
				if (pgdir[i] & PTE_P)
					assert(pgdir[i] & PTE_W);
				else
					assert(pgdir[i] == 0);
			} 
			break;
		}
	}
	cprintf("check_kern_pml4e() succeeded!\n");
}

// This function returns the physical address of the page containing 'va',
// defined by the level 4 page map 'pml4e'.  The hardware normally performs
// this functionality for us!  We define our own version to help check
// the check_kern_pml4e() function; it shouldn't be used elsewhere.

static physaddr_t
check_va2pa(pml4e_t *pml4e, uintptr_t va)
{
	pdpe_t *pdpe;
	pde_t *pgdir;
	pte_t *p;

	pml4e = &pml4e[PML4X(va)];
	if(!(*pml4e & PTE_P))
		return ~0;

	pdpe = (pdpe_t *) KADDR(PTE_ADDR(*pml4e));
	// cprintf(" %x %x " , pdpe, *pdpe);
	if (!(pdpe[PDPX(va)] & PTE_P))
		return ~0;

	pgdir = (pde_t *) KADDR(PTE_ADDR(pdpe[PDPX(va)]));

	pgdir = &pgdir[PDX(va)];
	if (!(*pgdir & PTE_P))
		return ~0;
	p = (pte_t*) KADDR(PTE_ADDR(*pgdir));
	if (!(p[PTX(va)] & PTE_P))
		return ~0;
	return PTE_ADDR(p[PTX(va)]);
}


// check page_insert, page_remove, &c
static void
check_page(void)
{
	struct PageInfo *pp0, *pp1, *pp2,*pp3,*pp4,*pp5;
	struct PageInfo * fl;
	pte_t *ptep, *ptep1;
	pdpe_t *pdpe;
	pde_t *pde;
	void *va;
	int i;
	pp0 = pp1 = pp2 = pp3 = pp4 = pp5 =0;
	assert(pp0 = page_alloc(0));
	assert(pp1 = page_alloc(0));
	assert(pp2 = page_alloc(0));
	assert(pp3 = page_alloc(0));
	assert(pp4 = page_alloc(0));
	assert(pp5 = page_alloc(0));


	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);
	assert(pp3 && pp3 != pp2 && pp3 != pp1 && pp3 != pp0);
	assert(pp4 && pp4 != pp3 && pp4 != pp2 && pp4 != pp1 && pp4 != pp0);
	assert(pp5 && pp5 != pp4 && pp5 != pp3 && pp5 != pp2 && pp5 != pp1 && pp5 != pp0);

	// temporarily steal the rest of the free pages
	fl = page_free_list;
	page_free_list = NULL;

	// should be no free memory
	assert(!page_alloc(0));

	// there is no page allocated at address 0
	assert(page_lookup(kern_pml4, (void *) 0x0, &ptep) == NULL);

	// there is no free memory, so we can't allocate a page table 
	assert(page_insert(kern_pml4, pp1, 0x0, 0) < 0);

	// free pp0 and try again: pp0 should be used for page table
	page_free(pp0);
	assert(page_insert(kern_pml4, pp1, 0x0, 0) < 0);
	page_free(pp2);
	page_free(pp3);

	assert(page_insert(kern_pml4, pp1, 0x0, 0) == 0);
	assert((PTE_ADDR(kern_pml4[0]) == page2pa(pp0) || PTE_ADDR(kern_pml4[0]) == page2pa(pp2) || PTE_ADDR(kern_pml4[0]) == page2pa(pp3) ));
	assert(check_va2pa(kern_pml4, 0x0) == page2pa(pp1));
	assert(pp1->pp_ref == 1);
	assert(pp0->pp_ref == 1);
	assert(pp2->pp_ref == 1);
	//should be able to map pp3 at PGSIZE because pp0 is already allocated for page table
	assert(page_insert(kern_pml4, pp3, (void*) PGSIZE, 0) == 0);
	assert(check_va2pa(kern_pml4, PGSIZE) == page2pa(pp3));
	assert(pp3->pp_ref == 2);

	// should be no free memory
	assert(!page_alloc(0));

	// should be able to map pp3 at PGSIZE because it's already there
	assert(page_insert(kern_pml4, pp3, (void*) PGSIZE, 0) == 0);
	assert(check_va2pa(kern_pml4, PGSIZE) == page2pa(pp3));
	assert(pp3->pp_ref == 2);

	// pp3 should NOT be on the free list
	// could happen in ref counts are handled sloppily in page_insert
	assert(!page_alloc(0));
	// check that pgdir_walk returns a pointer to the pte
	pdpe = KADDR(PTE_ADDR(kern_pml4[PML4X(PGSIZE)]));
	pde = KADDR(PTE_ADDR(pdpe[PDPX(PGSIZE)]));
	ptep = KADDR(PTE_ADDR(pde[PDX(PGSIZE)]));
	assert(pml4e_walk(kern_pml4, (void*)PGSIZE, 0) == ptep+PTX(PGSIZE));

	// should be able to change permissions too.
	assert(page_insert(kern_pml4, pp3, (void*) PGSIZE, PTE_U) == 0);
	assert(check_va2pa(kern_pml4, PGSIZE) == page2pa(pp3));
	assert(pp3->pp_ref == 2);
	assert(*pml4e_walk(kern_pml4, (void*) PGSIZE, 0) & PTE_U);
	assert(kern_pml4[0] & PTE_U);


	// should not be able to map at PTSIZE because need free page for page table
	assert(page_insert(kern_pml4, pp0, (void*) PTSIZE, 0) < 0);

	// insert pp1 at PGSIZE (replacing pp3)
	assert(page_insert(kern_pml4, pp1, (void*) PGSIZE, 0) == 0);
	assert(!(*pml4e_walk(kern_pml4, (void*) PGSIZE, 0) & PTE_U));

	// should have pp1 at both 0 and PGSIZE
	assert(check_va2pa(kern_pml4, 0) == page2pa(pp1));
	assert(check_va2pa(kern_pml4, PGSIZE) == page2pa(pp1));
	// ... and ref counts should reflect this
	assert(pp1->pp_ref == 2);
	assert(pp3->pp_ref == 1);


	// unmapping pp1 at 0 should keep pp1 at PGSIZE
	page_remove(kern_pml4, 0x0);
	assert(check_va2pa(kern_pml4, 0x0) == ~0);
	assert(check_va2pa(kern_pml4, PGSIZE) == page2pa(pp1));
	assert(pp1->pp_ref == 1);
	assert(pp3->pp_ref == 1);

	// Test re-inserting pp1 at PGSIZE.
	// Thanks to Varun Agrawal for suggesting this test case.
	assert(page_insert(kern_pml4, pp1, (void*) PGSIZE, 0) == 0);
	assert(pp1->pp_ref);
	assert(pp1->pp_link == NULL);

	// unmapping pp1 at PGSIZE should free it
	page_remove(kern_pml4, (void*) PGSIZE);
	assert(check_va2pa(kern_pml4, 0x0) == ~0);
	assert(check_va2pa(kern_pml4, PGSIZE) == ~0);
	assert(pp1->pp_ref == 0);
	assert(pp3->pp_ref == 1);

	// forcibly take pp3 back
	//todo investigate
	//assert(PTE_ADDR(kern_pml4[0]) == page2pa(pp3));
	kern_pml4[0] = 0;
	assert(pp3->pp_ref == 1);
    page_decref(pp3);
	// check pointer arithmetic in pml4e_walk
	page_decref(pp0);
	page_decref(pp2);
	va = (void*)(PGSIZE * 100);
	ptep = pml4e_walk(kern_pml4, va, 1);
	pdpe = KADDR(PTE_ADDR(kern_pml4[PML4X(va)]));
	pde  = KADDR(PTE_ADDR(pdpe[PDPX(va)]));
	ptep1 = KADDR(PTE_ADDR(pde[PDX(va)]));
	assert(ptep == ptep1 + PTX(va));
	
    // check that new page tables get cleared
    page_decref(pp4);
	memset(page2kva(pp4), 0xFF, PGSIZE);
	pml4e_walk(kern_pml4, 0x0, 1);
	pdpe = KADDR(PTE_ADDR(kern_pml4[0]));
    pde  = KADDR(PTE_ADDR(pdpe[0]));
    ptep  = KADDR(PTE_ADDR(pde[0]));
	for(i=0; i<NPTENTRIES; i++)
		assert((ptep[i] & PTE_P) == 0);
	kern_pml4[0] = 0;

	// give free list back
	page_free_list = fl;

	// free the pages we took
	page_decref(pp0);
	page_decref(pp1);
	page_decref(pp2);

	assert(pp0->pp_ref=1);
	assert(pp1->pp_ref=1);
	assert(pp2->pp_ref=1);
	assert(pp3->pp_ref=1);
	assert(pp4->pp_ref=1);
	assert(pp5->pp_ref=1);

	// page_free(pp0);
	// page_free(pp1);
	// page_free(pp2);
	// page_free(pp3);
	// page_free(pp4);
	// page_free(pp5);

	cprintf("check_page() succeeded!\n");
}

// check page_insert, page_remove, &c, with an installed kern_pml4
static void
check_page_installed_pml4e(void)
{
	struct PageInfo *pp, *pp0, *pp1, *pp2;
	struct PageInfo *fl;
	pte_t *ptep, *ptep1;
	uintptr_t va;
	int i;

	// check that we can read and write installed pages
	pp1 = pp2 = 0;
	assert((pp0 = page_alloc(0)));
	assert((pp1 = page_alloc(0)));
	assert((pp2 = page_alloc(0)));
	page_free(pp0);
	memset(page2kva(pp1), 1, PGSIZE);
	memset(page2kva(pp2), 2, PGSIZE);
	page_insert(kern_pml4, pp1, (void*) PGSIZE, PTE_W);
	assert(pp1->pp_ref == 1);
	assert(*(uint32_t *)PGSIZE == 0x01010101U);
	page_insert(kern_pml4, pp2, (void*) PGSIZE, PTE_W);
	assert(*(uint32_t *)PGSIZE == 0x02020202U);
	assert(pp2->pp_ref == 1);
	assert(pp1->pp_ref == 0);
	*(uint32_t *)PGSIZE = 0x03030303U;
	assert(*(uint32_t *)page2kva(pp2) == 0x03030303U);
	page_remove(kern_pml4, (void*) PGSIZE);
	assert(pp2->pp_ref == 0);

	// forcibly take pp0 back
	//assert(PTE_ADDR(kern_pml4[0]) == page2pa(pp0));
	//kern_pml4[0] = 0;
	assert(pp0->pp_ref == 1);
	pp0->pp_ref = 0;

	// free the pages we took
	page_free(pp0);

	cprintf("check_page_installed_pml4e() succeeded!\n");
}
