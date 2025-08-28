#ifndef JOS_INC_MEMLAYOUT_H
#define JOS_INC_MEMLAYOUT_H

#ifndef __ASSEMBLER__
#include <inc/types.h>
#include <inc/mmu.h>
#endif /* not __ASSEMBLER__ */

/*
 * This file contains definitions for memory management in our OS,
 * which are relevant to both the kernel and user-mode software.
 */

// Global descriptor numbers
#define GD_KT     0x08     // kernel text
#define GD_KD     0x10     // kernel data
#define GD_UT     0x18     // user text
#define GD_UD     0x20     // user data
#define GD_TSS0   0x28     // Task segment selector for CPU 0
#define GD_TSS1   0x30     // continued

/*
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *                    .             .             .
 *                    .             .             .
 *                    .             .             .
 *   4 Gig,           |  Cur. Page Table (User R-)| R-/R-
 *     UVPT --------> +---------------------------+
 *                    |                           | RW/--
 *                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                    :             .             :
 *                    :             .             :
 *                    :             .             :
 *                    |~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                    |                           | RW/--
 *                    |  Remapped Physical Memory | RW/--
 *    3 Gig,          |                           | RW/--
 *    KERNBASE, ----> +---------------------------+ 0xc0000000 -------------+
 *    KSTACKTOP       |    CPU0's Kernel Stack    | RW/--  KSTKSIZE (32Kb)  | 
 *                    |- - - - - - - - - - - - - -|                         |
 *                    |     Invalid Memory (*)    | --/--    KSTKGAP(32Kb)  | 
 *                    +---------------------------+                         |
 *                    |    CPU1's Kernel Stack    | RW/--  KSTKSIZE (32Kb)  | 
 *                    |- - - - - - - - - - - - - -|                         + PTSIZE 2Mb
 *                    |     Invalid Memory (*)    | --/--    KSTKGAP(32Kb)  | 
 *                    +---------------------------+                         |
 *                    :             .             :                         |
 *                    :             .             :                         |
 *    MMIOLIM ------> +---------------------------+ 0xbfe00000 -------------+  
 *                    |      Memory-mapped I/O    | RW/--                   + PTSIZE 2Mb
 * ULIM, MMIOBASE --> +---------------------------+ 0xbfc00000 -------------+  
 *                    |         RO PAGES          | R-/R-                   + 25* PTSIZE 50Mb
 *    UPAGES    ----> +---------------------------+ 0xbca00000 ------+------+
 *                    |          RO ENVS          | R-/R-                   + PTSIZE 2Mb
 * UTOP,UENVS ------> +---------------------------+ 0xbc800000 -------------+  
 * UXSTACKTOP -/      |    User Exception Stack   | RW/RW                   + PGSIZE 4Kb
 * USTACKTOP  ------> +---------------------------+ 0xbc7fe000 -------------+
 *                    |     Normal User Stack     | RW/RW                   + PGSIZE 4Kb 
 * USTACKBOTTOM  ---> +---------------------------+ 0xbebfd000 -------------
 *                    |                           |
 *                    |                           |
 *                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                    .                           .
 *                    .                           .
 *                    .                           .
 *                    |~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                    |    Program Data & Heap    |
 *    UTEXT --------> +---------------------------+ 0x00800000 (8Mb)--------+
 *                    |                           |                         |
 *    PFTEMP -------> |---------------------------| 0x005ff000 (6Mb-4Kb)    +  4Mb 
 *                    |      Empty Memory (*)     |                         |
 *    UTEMP --------> +---------------------------+ 0x00400000 (4Mb)--------+
 *                    |      Empty Memory (*)     |                         |
 *                    |- - - - - - - - - - - - - -|                         + PTSIZE 2Mb
 *                    | User STAB Data (optional) |                         |
 *    USTABDATA ----> +---------------------------+ 0x00200000 (2Mb) -------+
 *                    |      Empty Memory (*)     |                         |
 *    EXTPHYSMEM ---> +---------------------------+ 0x00100000 (1Mb)        |
 *                    |       Reserved Memory     |                         + PTSIZE 2Mb
 *                    |     (BIOS, Device, VGA)   |                         |
 *    IOPHYSMEM ----> +---------------------------+ 0x000A0000 (640Kb)      |
 *                    |      Empty Memory (*)     |                         |
 *    0 ------------> +---------------------------+ ------------------------+
 *
 * (*) Note: The kernel ensures that "Invalid Memory" is *never* mapped.
 *     "Empty Memory" is normally unmapped, but user programs may map pages
 *     there if desired.  JOS user programs map pages temporarily at UTEMP.
 */


// All physical memory mapped at this address
#define	KERNBASE	0xC0000000

// At IOPHYSMEM (640K) there is a 384K hole for I/O.  From the kernel,
// IOPHYSMEM can be addressed at KERNBASE + IOPHYSMEM.  The hole ends
// at physical address EXTPHYSMEM.
#define IOPHYSMEM	0x0A0000
#define EXTPHYSMEM	0x100000

// Kernel stack.
#define KSTACKTOP	KERNBASE
#define KSTKSIZE	(8*PGSIZE)   		// size of a kernel stack
#define KSTKGAP		(8*PGSIZE)   		// size of a kernel stack guard

// Memory-mapped IO.
#define MMIOLIM		(KSTACKTOP - PTSIZE)
#define MMIOBASE	(MMIOLIM - PTSIZE)

#define ULIM		(MMIOBASE)

/*
 * User read-only mappings! Anything below here til UTOP are readonly to user.
 * They are global pages mapped in at env allocation time.
 */

// User read-only virtual page table (see 'uvpt' below)
#define UVPT		0x10000000000
// Read-only copies of the Page structures
#define UPAGES		(ULIM - 25*PTSIZE)
// Read-only copies of the global env structures
#define UENVS		(UPAGES - PTSIZE)

/*
 * Top of user VM. User can manipulate VA from UTOP-1 and down!
 */

// Top of user-accessible VM
#define UTOP		UENVS
// Top of one-page user exception stack
#define UXSTACKTOP	UTOP
// Next page left invalid to guard against exception stack overflow; then:
// Top of normal user stack
#define USTACKTOP	(UTOP - 2*PGSIZE)

// Where user programs generally begin
#define UTEXT		(4*PTSIZE)

// Used for temporary page mappings.  Typed 'void*' for convenience
#define UTEMP		((void*) (PTSIZE*2))
// Used for temporary page mappings for the user page-fault handler
// (should not conflict with other temporary page mappings)
#define PFTEMP		(UTEMP + PTSIZE - PGSIZE)
// The location of the user-level STABS data structure
#define USTABDATA	(PTSIZE)

// Physical address of startup code for non-boot CPUs (APs)
#define MPENTRY_PADDR	0x7000

#ifndef __ASSEMBLER__

typedef uint64_t pte_t;
typedef uint64_t pde_t;
typedef uint64_t pdpe_t;
typedef uint64_t pml4e_t;

#if JOS_USER
/*
 * The page directory entry corresponding to the virtual address range
 * [UVPT, UVPT + PTSIZE) points to the page directory itself.  Thus, the page
 * directory is treated as a page table as well as a page directory.
 *
 * One result of treating the page directory as a page table is that all PTEs
 * can be accessed through a "virtual page table" at virtual address UVPT (to
 * which uvpt is set in lib/entry.S).  The PTE for page number N is stored in
 * uvpt[N].  (It's worth drawing a diagram of this!)
 *
 * A second consequence is that the contents of the current page directory
 * will always be available at virtual address (UVPT + (UVPT >> PGSHIFT)), to
 * which uvpd is set in lib/entry.S.
 */
extern volatile pte_t uvpt[];     // VA of "virtual page table"
extern volatile pde_t uvpd[];     // VA of current page directory
extern volatile pde_t uvpde[];    // VA of current page directory pointer
extern volatile pde_t uvpml4e[];  // VA of current page map level 4

#endif

/*
 * Page descriptor structures, mapped at UPAGES.
 * Read/write to the kernel, read-only to user programs.
 *
 * Each struct PageInfo stores metadata for one physical page.
 * Is it NOT the physical page itself, but there is a one-to-one
 * correspondence between physical pages and struct PageInfo's.
 * You can map a struct PageInfo * to the corresponding physical address
 * with page2pa() in kern/pmap.h.
 */
struct PageInfo {
	// Next page on the free list.
	struct PageInfo *pp_link;

	// pp_ref is the count of pointers (usually in page table entries)
	// to this page, for pages allocated using page_alloc.
	// Pages allocated at boot time using pmap.c's
	// boot_alloc do not have valid reference count fields.

	uint16_t pp_ref;
};

#endif /* !__ASSEMBLER__ */
#endif /* !JOS_INC_MEMLAYOUT_H */

//https://wiki.osdev.org/Paging
