#include <inc/mmu.h>
#include <inc/memlayout.h>

pml4e_t entry_pml4e[NPTENTRIES];
pdpe_t entry_pdpt[NPTENTRIES];
// pde_t entry_pgdir[NPTENTRIES];
// pte_t entry_pgtable[NPTENTRIES];

//4MB->2MB on 64 bit
//2 -> 4 levels of tables

// The entry.S page directory maps the first 4MB of physical memory
// starting at virtual address KERNBASE (that is, it maps virtual
// addresses [KERNBASE, KERNBASE+4MB) to physical addresses [0, 4MB)).
// We choose 4MB because that's how much we can map with one page
// table and it's enough to get us through early boot.  We also map
// virtual addresses [0, 4MB) to physical addresses [0, 4MB); this
// region is critical for a few instructions in entry.S and then we
// never use it again.
//
// Page directories (and page tables), must start on a page boundary,
// hence the "__aligned__" attribute.  Also, because of restrictions
// related to linking and static initializers, we use "x + PTE_P"
// here, rather than the more standard "x | PTE_P".  Everywhere else
// you should use "|" to combine flags.
//
// mapping is repeated at each level even though it only happens at 
// one level to handle kernbase being located

__attribute__((__aligned__(PGSIZE)))
pml4e_t entry_pml4e[NPDENTRIES] = {
    // Map VA's [0, 4MB) to PA's [0, 4MB)
    [0] = ((uintptr_t)entry_pdpt - KERNBASE) + PTE_P,
    // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
    [PML4X(KERNBASE)] = ((uintptr_t)entry_pdpt - KERNBASE) + PTE_P + PTE_W
};

__attribute__((__aligned__(PGSIZE)))
pdpe_t entry_pdpt[NPTENTRIES] = {
    // Map VA's [0, 1GB) to PA's [0, 1GB) using 1GB pages
    [0] = (0x000000000 | PTE_P | PTE_W | PTE_PS),
    // Map VA's [KERNBASE, KERNBASE+1GB) to PA's [0, 1GB) using 1GB pages
    [PDPX(KERNBASE)] = (0x000000000 | PTE_P | PTE_W | PTE_PS)
};
// __attribute__((__aligned__(PGSIZE)))
// pde_t entry_pgdir[NPTENTRIES] = {
//     // Map VA's [0, 4MB) to PA's [0, 4MB) using 2MB pages
//     [0] = (0x000000 | PTE_P | PTE_W | PTE_PS),
//     [1] = (0x200000 | PTE_P | PTE_W | PTE_PS),
//     [2] = (0x400000 | PTE_P | PTE_W | PTE_PS),
//     [3] = (0x600000 | PTE_P | PTE_W | PTE_PS),
//     // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB) using 2MB pages
//     [PDX(KERNBASE)] = (0x000000 | PTE_P | PTE_W | PTE_PS),
//     [PDX(KERNBASE)+1] = (0x200000 | PTE_P | PTE_W | PTE_PS),
//     [PDX(KERNBASE)+2] = (0x400000 | PTE_P | PTE_W | PTE_PS),
//     [PDX(KERNBASE)+2] = (0x600000 | PTE_P | PTE_W | PTE_PS)
// };

// __attribute__((__aligned__(PGSIZE)))
// pde_t entry_pgdir[NPDENTRIES] = {
// 	// Map VA's [0, 2MB) to PA's [0, 2MB)
// 	[0]
// 		= ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P,
// 	// Map VA's [KERNBASE, KERNBASE+2MB) to PA's [0, 2MB)
// 	[KERNBASE>>PDXSHIFT]
// 		= ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P + PTE_W
// };

