/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/elf.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/monitor.h>
#include <kern/sched.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>

struct Env *envs = NULL;		// All environments
static struct Env *env_free_list;	// Free environment list
					// (linked by Env->env_link)

#define ENVGENSHIFT	12		// >= LOGNENV

// Global descriptor table.
//
// Set up global descriptor table (GDT) with separate segments for
// kernel mode and user mode.  Segments serve many purposes on the x86.
// We don't use any of their memory-mapping capabilities, but we need
// them to switch privilege levels. 
//
// The kernel and user segments are identical except for the DPL.
// To load the SS register, the CPL must equal the DPL.  Thus,
// we must duplicate the segments for the user and the kernel.
//
// In particular, the last argument to the SEG macro used in the
// definition of gdt specifies the Descriptor Privilege Level (DPL)
// of that descriptor: 0 for kernel and 3 for user.
//
//5 + 2 entry tss per cpu
struct Segdesc gdt[5+ 2*NCPU] =
{
	// 0x0 - unused (always faults -- for trapping NULL far pointers)
	SEG_NULL,

	// 0x8 - kernel code segment
	[GD_KT >> 3] = SEG64(STA_X | STA_R, 0x0, 0xffffffff,0),

	// 0x10 - kernel data segment
	[GD_KD >> 3] = SEG64(STA_W, 0x0, 0xffffffff,0),

	// 0x18 - user code segment
	[GD_UT >> 3] = SEG64(STA_X | STA_R, 0x0, 0xffffffff,3),

	// 0x20 - user data segment
	[GD_UD >> 3] = SEG64(STA_W, 0x0, 0xffffffff,3),

	// 0x28 - tss, initialized in trap_init_percpu()
	[GD_TSS0 >> 3] = SEG_NULL,

	[GD_TSS1 >> 3] = SEG_NULL,
	};

struct Pseudodesc gdt_pd = {
	sizeof(gdt) - 1, (uint64_t) gdt
};

//
// Converts an envid to an env pointer.
// If checkperm is set, the specified environment must be either the
// current environment or an immediate child of the current environment.
//
// RETURNS
//   0 on success, -E_BAD_ENV on error.
//   On success, sets *env_store to the environment.
//   On error, sets *env_store to NULL.
//
int
envid2env(envid_t envid, struct Env **env_store, bool checkperm)
{
	struct Env *e;

	// If envid is zero, return the current environment.
	if (envid == 0) {
		*env_store = curenv;
		return 0;
	}

	// Look up the Env structure via the index part of the envid,
	// then check the env_id field in that struct Env
	// to ensure that the envid is not stale
	// (i.e., does not refer to a _previous_ environment
	// that used the same slot in the envs[] array).
	e = &envs[ENVX(envid)];
	if (e->env_status == ENV_FREE || e->env_id != envid) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	// Check that the calling environment has legitimate permission
	// to manipulate the specified environment.
	// If checkperm is set, the specified environment
	// must be either the current environment
	// or an immediate child of the current environment.
	if (checkperm && e != curenv && e->env_parent_id != curenv->env_id) {
		*env_store = 0;
		return -E_BAD_ENV;
	}

	*env_store = e;
	return 0;
}

// Mark all environments in 'envs' as free, set their env_ids to 0,
// and insert them into the env_free_list.
// Make sure the environments are in the free list in the same order
// they are in the envs array (i.e., so that the first call to
// env_alloc() returns envs[0]).
//
/*
- Similar to page_free_list from Lab 2
	- List was created with a loop that updated the head at every iteration, so last page added was first
- Same loop can be applied, but this time we must insert all NENVS records from the envs page in env_free_list
- Records must be inserted in reverse order just like page_free_list
*/
void
env_init(void)
{
	// Set up envs array
	// LAB 3: Your code here.

	int i;
    env_free_list = NULL;
    
    // reverse order insertion
    for (i = NENV - 1; i >= 0; i--) {
        envs[i].env_id = 0;
        envs[i].env_status = ENV_FREE;
        envs[i].env_link = env_free_list;
        env_free_list = &envs[i];
    }

	// Per-CPU part of the initialization
	env_init_percpu();
}

// Load GDT and segment descriptors.
void
env_init_percpu(void)
{
	lgdt(&gdt_pd);
	// The kernel never uses GS or FS, so we leave those set to
	// the user data segment.
	asm volatile("movw %%ax,%%gs" : : "a" (GD_UD|3));
	asm volatile("movw %%ax,%%fs" : : "a" (GD_UD|3));
	// The kernel does use ES, DS, and SS.  We'll change between
	// the kernel and user data segments as needed.
	asm volatile("movw %%ax,%%es" : : "a" (GD_KD));
	asm volatile("movw %%ax,%%ds" : : "a" (GD_KD));
	asm volatile("movw %%ax,%%ss" : : "a" (GD_KD));
	// Load the kernel text segment into CS.
	// asm volatile("ljmp %0,$1f\n 1:\n" : : "i" (GD_KT));
	asm volatile("pushq %%rbx \n \t movabs $1f,%%rax \n \t pushq %%rax \n\t lretq \n 1:\n" :: "b" (GD_KT):"cc","memory");
	// For good measure, clear the local descriptor table (LDT),
	// since we don't use it.
	lldt(0);
}

//
// Initialize the kernel virtual memory layout for environment e.
// Allocate a page directory, set e->env_pgdir accordingly,
// and initialize the kernel portion of the new environment's address space.
// Do NOT (yet) map anything into the user portion
// of the environment's virtual address space.
//
// Returns 0 on success, < 0 on error.  Errors include:
//	-E_NO_MEM if page directory or table could not be allocated.
//
/*
- Allocates a page directory at 4th level (env_pml4e)
- User environments will have pages that must be shared with other envs, so
  we must copy the pages in kernel page directory in a shared area and make them read-only
- DO NOT use memcpy to copy kern_pml4 into the new environment
- Instead. copy kernel half
- Walk virtual addresses in a loop from UTO to KERNBASE-1
- For each address, loop up existing mapping in kern_pml4 with pml4e_walk
	- If a page iss found and available (PTE_P), create corresponding entry in environment's page tables
	  (e->env_pml4e) using pml4e_walk() with create=1
- Copy page
- Remainder will remain unmapped; each env will fill it in later as needed
- After loop, mirror kernel space with:
	pdpe_t *env_pdpe = KADDR(PTE_ADDR(e->env_pml4e[0]));
	pde_t *kern_pdpe = KADDR(PTE_ADDR(kern_pml4[0]));
	env_pdpe[3]= kern_pdpe[3];
- Finalize function by setting environment's UVPT page with read-only perms
	e->env_pml4e[PML4X(UVPT)] = PADDR(e->env_pml4e) | PTE_P | PTE_U;`
	return 0;
*/
static int
env_setup_vm(struct Env *e)
{
	int i;
	struct PageInfo *p = NULL;

	// Allocate a page for the page directory
	if (!(p = page_alloc(ALLOC_ZERO)))
		return -E_NO_MEM;

	// Now, set e->env_pgdir and initialize the page directory.
	//
	// Hint:
	//    - The VA space of all envs is identical above UTOP
	//	(except at UVPT, which we've set below).
	//	See inc/memlayout.h for permissions and layout.
	//	Hint: Figure out which entry in the pml4e maps addresses 
    //	      above UTOP.
	//	(Make sure you got the permissions right in Lab 2.)
	//    - The initial VA below UTOP is empty.
	//    - You do not need to make any more calls to page_alloc.
	//    - Note: In general, pp_ref is not maintained for
	//	physical pages mapped only above UTOP, but env_pgdir
	//	is an exception -- you need to increment env_pgdir's
	//	pp_ref for env_free to work correctly.
	//    - The functions in kern/pmap.h are handy.

	// LAB 3: Your code here.
	p->pp_ref++;
	e->env_pml4e = (pml4e_t *)page2kva(p);

	uintptr_t va;
    for (va = UTOP; va < KERNBASE; va += PGSIZE) {
        pte_t *kern_pte = pml4e_walk(kern_pml4, (void *)va, 0);
        
        // if mapping already exists in kernel page table
        if (kern_pte && (*kern_pte & PTE_P)) {
            // create corresponding entry in env page table
            pte_t *env_pte = pml4e_walk(e->env_pml4e, (void *)va, 1);
            
            if (env_pte == NULL) {
                page_decref(p);
                return -E_NO_MEM;
            }
            
            *env_pte = *kern_pte;
        }
    }

	if (!(e->env_pml4e[0] & PTE_P)) {
        struct PageInfo *pdpe_page = page_alloc(ALLOC_ZERO);
        if (pdpe_page == NULL) {
            page_decref(p);
            return -E_NO_MEM;
        }
        pdpe_page->pp_ref++;
        e->env_pml4e[0] = page2pa(pdpe_page) | PTE_P | PTE_U | PTE_W;
    }
    
    // mirror kernel space
    pdpe_t *env_pdpe = KADDR(PTE_ADDR(e->env_pml4e[0]));
    pdpe_t *kern_pdpe = KADDR(PTE_ADDR(kern_pml4[0]));
    env_pdpe[3] = kern_pdpe[3];

	// UVPT maps the env's own page table read-only.
	// Permissions: kernel R, user R
	e->env_pml4e[PML4X(UVPT)] = PADDR(e->env_pml4e) | PTE_P | PTE_U;
	return 0;
}

//
// Allocates and initializes a new environment.
// On success, the new environment is stored in *newenv_store.
//
// Returns 0 on success, < 0 on failure.  Errors include:
//	-E_NO_FREE_ENV if all NENV environments are allocated
//	-E_NO_MEM on memory exhaustion
//
int
env_alloc(struct Env **newenv_store, envid_t parent_id)
{
	int32_t generation;
	int r;
	struct Env *e;

	if (!(e = env_free_list))
		return -E_NO_FREE_ENV;

	// Allocate and set up the page directory for this environment.
	if ((r = env_setup_vm(e)) < 0)
		return r;

	// Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)	// Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);

	// Set the basic status variables.
	e->env_parent_id = parent_id;
	e->env_type = ENV_TYPE_USER;
	e->env_status = ENV_RUNNABLE;
	e->env_runs = 0;

	// Clear out all the saved register state,
	// to prevent the register values
	// of a prior environment inhabiting this Env structure
	// from "leaking" into our new environment.
	memset(&e->env_tf, 0, sizeof(e->env_tf));

	// Set up appropriate initial values for the segment registers.
	// GD_UD is the user data segment selector in the GDT, and
	// GD_UT is the user text segment selector (see inc/memlayout.h).
	// The low 2 bits of each segment register contains the
	// Requestor Privilege Level (RPL); 3 means user mode.  When
	// we switch privilege levels, the hardware does various
	// checks involving the RPL and the Descriptor Privilege Level
	// (DPL) stored in the descriptors themselves.
	// e->env_tf.tf_ds = GD_UD | 3;
	// e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_rsp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;
	// You will set e->env_tf.tf_rip later.

	// Enable interrupts while in user mode.
	// LAB 4: Your code here.
	e->env_tf.tf_eflags |= FL_IF;

	// Clear the page fault handler until user installs one.
	e->env_pgfault_upcall = 0;

	// Also clear the IPC receiving flag.
	e->env_ipc_recving = 0;

	// commit the allocation
	env_free_list = e->env_link;
	*newenv_store = e;

	cprintf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
	return 0;
}

//
// Allocate len bytes of physical memory for environment env,
// and map it at virtual address va in the environment's address space.
// Does not zero or otherwise initialize the mapped pages in any way.
// Pages should be writable by user and kernel.
// Panic if any allocation attempt fails.
/*
*/
static void
region_alloc(struct Env *e, void *va, size_t len)
{
	// LAB 3: Your code here.
	// (But only if you need it for load_icode.)
	//
	// Hint: It is easier to use region_alloc if the caller can pass
	//   'va' and 'len' values that are not page-aligned.
	//   You should round va down, and round (va + len) up.
	//   (Watch out for corner-cases!)
	void *va_start = ROUNDDOWN(va, PGSIZE);
	
    void *addr;
	void *va_end = ROUNDUP(va + len, PGSIZE);
    for (addr = va_start; addr < va_end; addr += PGSIZE) {
        struct PageInfo *pp = page_alloc(0); // allocate a page without zeroing
        
        if (pp == NULL) {
            panic("region_alloc: page_alloc failed");
        }
        int r = page_insert(e->env_pml4e, pp, addr, PTE_U | PTE_W); // user + writeable
        
        if (r < 0) {
            panic("region_alloc: page_insert failed");
        }
    }
}

//
// Set up the initial program binary, stack, and processor flags
// for a user process.
// This function is ONLY called during kernel initialization,
// before running the first user-mode environment.
//
// This function loads all loadable segments from the ELF binary image
// into the environment's user memory, starting at the appropriate
// virtual addresses indicated in the ELF program header.
// At the same time it clears to zero any portions of these segments
// that are marked in the program header as being mapped
// but not actually present in the ELF file - i.e., the program's bss section.
//
// All this is very similar to what our boot loader does, except the boot
// loader also needs to read the code from disk.  Take a look at
// boot/main.c to get ideas.
//
// Finally, this function maps one page for the program's initial stack.
//
// load_icode panics if it encounters problems.
//  - How might load_icode fail?  What might be wrong with the given input?
//
/*
Notes:
- modify CR3 register to point to env's page directory: lcr3(PADDR(e->env_pml4e))
- Receives pointer to binary ELF file 
- Define ELF struct:
	struct Elf *elfhdr = (struct Elf *) binary;
- In ELF header:
	- Check that e_magic = ELF_MAGIC; if not PANIC AHHH
	- e_entry has address where program starts
	- e_phoff tells where program begins
	- e_phnum tells how many program headers there are
- Define program header struct:
	struct Proghdr *hdr
- In each program header:
	- Check p_type == ELF_PROG_LOAD; if not, skip
	- if valid, use region_alloc with (void *) p_va and p_memsz bytes to allocate memory
	- p_offset tells where segment begins in binary file
	- p_filesz tells how many bytes to copy from binary to memory
         memcpy((void *)hdr->p_va, binary + hdr->p_offset, hdr->p_filesz);
- After segments loaded, function must allocate a page with zeroes below USTACKTOP for stack (region_alloc and memset)
- Inform env of address of program entry: e->env_tf.tf_rip = elfhdr->e_entry;
- At end: lcr3(PADDR(kern_pml4)); to switch back to kernel page directory

*/
static void
load_icode(struct Env *e, uint8_t *binary)
{
	// Hints:
	//  Load each program segment into virtual memory
	//  at the address specified in the ELF segment header.
	//  You should only load segments with ph->p_type == ELF_PROG_LOAD.
	//  Each segment's virtual address can be found in ph->p_va
	//  and its size in memory can be found in ph->p_memsz.
	//  The ph->p_filesz bytes from the ELF binary, starting at
	//  'binary + ph->p_offset', should be copied to virtual address
	//  ph->p_va.  Any remaining memory bytes should be cleared to zero.
	//  (The ELF header should have ph->p_filesz <= ph->p_memsz.)
	//  Use functions from the previous lab to allocate and map pages.
	//
	//  All page protection bits should be user read/write for now.
	//  ELF segments are not necessarily page-aligned, but you can
	//  assume for this function that no two segments will touch
	//  the same virtual page.
	//
	//  You may find a function like region_alloc useful.
	//
	//  Loading the segments is much simpler if you can move data
	//  directly into the virtual addresses stored in the ELF binary.
	//  So which page directory should be in force during
	//  this function?
	//
	//  You must also do something with the program's entry point,
	//  to make sure that the environment starts executing there.
	//  What?  (See env_run() and env_pop_tf() below.)

	// LAB 3: Your code here.
	struct Elf *elfhdr = (struct Elf *)binary;
	if (elfhdr->e_magic != ELF_MAGIC) {
        panic("load_icode: not a valid ELF file");
    }
	lcr3(PADDR(e->env_pml4e));

	struct Proghdr *ph = (struct Proghdr *)(binary + elfhdr->e_phoff);
	struct Proghdr *eph = ph + elfhdr->e_phnum;

	 for (; ph < eph; ph++) {
        if (ph->p_type != ELF_PROG_LOAD) {
            continue;
        }
        
        region_alloc(e, (void *)ph->p_va, ph->p_memsz);
        memcpy((void *)ph->p_va, binary + ph->p_offset, ph->p_filesz);
        // p_memsz >= p_filesz, so we zero (p_memsz - p_filesz) bytes
        memset((void *)(ph->p_va + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
    }

	e->env_tf.tf_rip = elfhdr->e_entry;

	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.
	
	// LAB 3: Your code here.
	region_alloc(e, (void *)(USTACKTOP - PGSIZE), PGSIZE);
	lcr3(PADDR(kern_pml4));
}

//
// Allocates a new env with env_alloc, loads the named elf
// binary into it with load_icode, and sets its env_type.
// This function is ONLY called during kernel initialization,
// before running the first user-mode environment.
// The new env's parent ID is set to 0.
//
/*
- Declare struct Env *newEnv and enable it with env_alloc
- Panic if env_alloc returns nonzero
- Otherwise load_icode() with newEnv and binary
- Store type value in newEnv->env_type
*/
void
env_create(uint8_t *binary, enum EnvType type)
{
	struct Env *newEnv;
    int r = env_alloc(&newEnv, 0);
    
    if (r < 0) {
        panic("env_create: env_alloc failed: %e returned instead of 0", r);
    }
    
    load_icode(newEnv, binary);
    newEnv->env_type = type;
}

//
// Frees env e and all memory it uses.
//
void
env_free(struct Env *e)
{
	pte_t *pt;
	uint64_t pdeno, pteno;
	physaddr_t pa;

	// If freeing the current environment, switch to kern_pgdir
	// before freeing the page directory, just in case the page
	// gets reused.
	if (e == curenv)
		lcr3(PADDR(kern_pml4));

	// Note the environment's demise.
	cprintf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);
	// cprintf("ULIM:%x\n",ULIM);
	// cprintf("UTOP:%x\n",UTOP);

	static_assert(UTOP % PTSIZE == 0);

	// Flush all mapped pages in the user portion of the address space
	pdpe_t *env_pdpe = KADDR(PTE_ADDR(e->env_pml4e[0]));
	int pdeno_limit;
	uint64_t pdpe_index;
	//using 3 instead of NPDPENTRIES as we only need to deal with first 3gbs
	for(pdpe_index=0;pdpe_index<3;pdpe_index++){
		if(!(env_pdpe[pdpe_index] & PTE_P))
			continue;
		pde_t *env_pgdir = KADDR(PTE_ADDR(env_pdpe[pdpe_index]));
		for (pdeno = 0; pdeno < NPDENTRIES; pdeno++) {

			// only look at mapped page tables
			if (!(env_pgdir[pdeno] & PTE_P))
				continue;
			// find the pa and va of the page table
			pa = PTE_ADDR(env_pgdir[pdeno]);
			pt = (pte_t*) KADDR(pa);

			// unmap all PTEs in this page table
			for (pteno = 0; pteno < NPDENTRIES &&PGADDR((uint64_t)0,pdpe_index,pdeno, pteno, 0)<(void*)UTOP; pteno++) {
				if (pt[pteno] & PTE_P){
					page_remove(e->env_pml4e, PGADDR((uint64_t)0,pdpe_index,pdeno, pteno, 0));
				}
			}

			// free the page table itself
			env_pgdir[pdeno] = 0;
			page_decref(pa2page(pa));
		}
		// free the page directory
		pa = PTE_ADDR(env_pdpe[pdpe_index]);
		env_pdpe[pdpe_index] = 0;
		page_decref(pa2page(pa));
	}
	// free the page directory pointer
	page_decref(pa2page(PTE_ADDR(e->env_pml4e[0])));

	// free the PML4
	pa = PADDR(e->env_pml4e);
	e->env_pml4e = 0;
	page_decref(pa2page(pa));

	// return the environment to the free list
	e->env_status = ENV_FREE;
	e->env_link = env_free_list;
	env_free_list = e;
}

//
// Frees environment e.
// If e was the current env, then runs a new environment (and does not return
// to the caller).
//
void
env_destroy(struct Env *e)
{
	// If e is currently running on other CPUs, we change its state to
	// ENV_DYING. A zombie environment will be freed the next time
	// it traps to the kernel.
	if (e->env_status == ENV_RUNNING && curenv != e) {
		e->env_status = ENV_DYING;
		return;
	}

	env_free(e);

	if (curenv == e) {
		curenv = NULL;
		sched_yield();
	}
}


//
// Restores the register values in the Trapframe with the 'iret' instruction.
// This exits the kernel and starts executing some environment's code.
//
// This function does not return.
//
//todo simplify
void
env_pop_tf(struct Trapframe *tf)
{
	// Record the CPU we are running on for user-space debugging
	curenv->env_cpunum = cpunum();
	asm volatile("movq %0,%%rsp\n"
			 "\tmovq 0(%%rsp),%%r15\n" \
			 "\tmovq 8(%%rsp),%%r14\n" \
			 "\tmovq 16(%%rsp),%%r13\n" \
			 "\tmovq 24(%%rsp),%%r12\n" \
			 "\tmovq 32(%%rsp),%%r11\n" \
			 "\tmovq 40(%%rsp),%%r10\n" \
			 "\tmovq 48(%%rsp),%%r9\n" \
			 "\tmovq 56(%%rsp),%%r8\n" \
			 "\tmovq 64(%%rsp),%%rsi\n" \
			 "\tmovq 72(%%rsp),%%rdi\n" \
			 "\tmovq 80(%%rsp),%%rbp\n" \
			 "\tmovq 88(%%rsp),%%rdx\n" \
			 "\tmovq 96(%%rsp),%%rcx\n" \
			 "\tmovq 104(%%rsp),%%rbx\n" \
			 "\tmovq 112(%%rsp),%%rax\n" \
			 "\taddq $120,%%rsp\n"
			 "\taddq $16,%%rsp\n" /* skip tf_trapno and tf_errcode */
			 "\tiretq"
			 : : "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}

//
// Context switch from curenv to env e.
// Note: if this is the first call to env_run, curenv is NULL.
//
// This function does not return.
//
void
env_run(struct Env *e)
{
	// Step 1: If this is a context switch (a new environment is running):
	//	   1. Set the current environment (if any) back to
	//	      ENV_RUNNABLE if it is ENV_RUNNING (think about
	//	      what other states it can be in),
	//	   2. Set 'curenv' to the new environment,
	//	   3. Set its status to ENV_RUNNING,
	//	   4. Update its 'env_runs' counter,
	//	   5. Use lcr3() to switch to its address space.
	// Step 2: Use env_pop_tf() to restore the environment's
	//	   registers and drop into user mode in the
	//	   environment.

	// Hint: This function loads the new environment's state from
	//	e->env_tf.  Go back through the code you wrote above
	//	and make sure you have set the relevant parts of
	//	e->env_tf to sensible values.

	// LAB 3: Your code here.
	if (curenv && curenv->env_status == ENV_RUNNING) {
		curenv->env_status = ENV_RUNNABLE;
	}
    
    curenv = e;
    curenv->env_status = ENV_RUNNING;
    curenv->env_runs++;
    
    lcr3(PADDR(curenv->env_pml4e));

	unlock_kernel();
    env_pop_tf(&curenv->env_tf);
}

