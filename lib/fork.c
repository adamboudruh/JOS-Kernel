// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

/*
Notes
- JOS uses 4 level page tables, each containing NPTENTRIES=512 entries of type pte_t
- JOS already created arrays that will make it easier to access these pages linearly
- uvpml4e[] has 512 entries, uvpde[] has 512*512 entries, uvpd[] has 512*512*512 entries, uvpt[] has 512*512*512*512 entries
- ith page in uvpl4e[] corresponds to 512 pages in uvpde; index (512*i) to (512*i + 511)
- Similarly, jth page in uvpde[] array corresponds to 512 entries in uvpd[]; index (512*(512*i+j)) to (512*(512*i+j) + 511)
- Finally, kth page in uvpd[] array corresponds to 512 entries in uvpt[]; index (512*(512*(512*i+j)+k)) to (512*(512*(512*i+j)+k) + 511)
	- lth page in uvpt: index (512*(512*(512*i+j)+k)+l)
*/

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
/*
- verify that faulting page in UTrapframe is produced by write op, err == ; if not painc
- obtain the page from uvpt[] using: pte_t pte = uvpt[PGNUM(addr)];
- verify that pte contains PT_CO flag; if not panic
- allocate a new read/write page in child env with sys_page_alloc()
	- PFTEMP can be temporary addrress
- copy contents of pte onto temp location (PFTEMP)
- map PFTEMP to address of faulting page (r/w perms) using sys_page_map()
- unmap PFTEMP using sys_page_unmap()
- panic if any step fails
*/
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	if (!(err & FEC_WR)) {
		panic("fault was not a write (err=%x, va=%p)", err, addr);
	}
	pte_t pte = uvpt[PGNUM(addr)];
	if (!(pte & PTE_COW)) {
		panic("pgfault: page is not marked copy-on-write (pte=%p, va=%p)", pte, addr);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.

	void *pg_addr = ROUNDDOWN(addr, PGSIZE);
	r = sys_page_alloc(0, PFTEMP, PTE_P | PTE_U | PTE_W);
	if (r < 0) {
		panic("pgfault: sys_page_alloc failed:  %e", r);
	}
	memmove(PFTEMP, pg_addr, PGSIZE);
	r = sys_page_map(0, PFTEMP, 0, pg_addr, PTE_P | PTE_U | PTE_W);
	if (r < 0) {
		panic("pgfault: sys_page_map failed:  %e", r);
	}
	r = sys_page_unmap(0, PFTEMP);
	if (r < 0) {
		panic("pgfault: sys_page_unmap failed:  %e", r);
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
/*
- receives an envid and prefix of a va (pn)
- called if page is writable or marked COW
Steps
- obtain va by binary shifting pn 12(=PGSHIFT) locations to left (<<)
- obtain page from uvpt  using uvpt[pn]
	- if page is PT_W or PT_COW, map va in current env onto envid as COW using sys_page_map(); return error code if fails
	- map va onto current environment (0) as COW using sys_page_map(); return error code if fails
- if uvpt[pn] is neither PTE_W nor PTE_COW, copy the mapping of va onto envid using sys_page_map() with PGOFF(uvpt[pn]) as perms; return error code if fails
*/
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	
	void *va = (void *)((uintptr_t)pn << PGSHIFT);
	pte_t pte = uvpt[pn];

	// Debug:  uncomment to see what's being mapped
	cprintf("duppage: pn=%x va=%p pte=%p\n", pn, va, pte);

	if ((pte & PTE_W) || (pte & PTE_COW)) {
		// Map to child first, then remap parent
		r = sys_page_map(0, va, envid, va, PTE_P | PTE_U | PTE_COW);
		if (r < 0) {
			panic("duppage: child map failed: %e, va=%p", r, va);
			return r;
		}

		r = sys_page_map(0, va, 0, va, PTE_P | PTE_U | PTE_COW);
		if (r < 0) {
			panic("duppage: parent remap failed: %e, va=%p", r, va);
			return r;
		}
	} else {
		r = sys_page_map(0, va, envid, va, PTE_P | PTE_U);
		if (r < 0) {
			panic("duppage: readonly map failed: %e, va=%p", r, va);
			return r;
		}
	}

	return 0;
}

extern void _pgfault_upcall();

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpml4e, uvpde, uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//   
/*
Steps
- setup page fault handler using set_pgfault_handler() and passing pgfault as param
- create childenv by calling sys_exofork(); forks environment into parent and child, code must expect both envids:
	- if envid value returns is negative, panic
	- if envid is 0: thisenv = &envs[ENVX(sys_getenvid())]; then return 0
- if envid returned is > 0, we are back in the parent env, meaning the envid returned is that of the child env
- now we must copy address space from parent to child
	- parent allocates one writeable page at UXSTACKTOP with sys_page_alloc(); return error code if fails
	- 4 level nested loop that reviews structure of the virtual space
Loop explained:
- first level reviews all 512 entries (NPTENTRIES) of uvpml4e[]; if entry is present (PTE_P), proceed to second level
	- second level reviews all 512 entries of uvpde[] that correspond to the first level
		- third level reviews all 512 entries of uvpd[] that correspond to the second level
			- fourth level reviews all 512 entries of uvpt[] that correspond to the third level
				- at fourth level, when a PTE_P is found, find address with: uintptr_t addr = (uintptr_t)PGADDR(i, j, k, l, 0);
				- if addr is in UXSTACKTOP, do nothing but keep searching
				- if page is beyond limits for our virtual memory (>= UTOP), stop search in all loops
				- otherwize, duppage() to copy that page onto child env and continue

- after loops, call sys_env_pgfault() to set _pfgfault_upcall in child env and call sys_env_set_status() to set child env as ENV_RUNNABLE; return code if fails
- 
*/
envid_t
fork(void)
{
	// LAB 4: Your code here.
	int r;
	envid_t envid;
	
	set_pgfault_handler(pgfault);
	envid = sys_exofork();
	if (envid < 0) {
		panic("sys_exofork failed: %e", envid);
	}
	
	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	
	// we are parent from here on
	// envid is the child's environment ID

	for (int i = 0; i < NPTENTRIES; i++) {
		if (!(uvpml4e[i] & PTE_P)) {
			continue;
		}
		
		for (int j = 0; j < NPTENTRIES; j++) {
			int pdpeIndex = i * NPTENTRIES + j;
			if (!(uvpde[pdpeIndex] & PTE_P)) {
				continue;
			}
			
			for (int k = 0; k < NPTENTRIES; k++) {
				int pdeIndex = pdpeIndex * NPTENTRIES + k;
				
				if (!(uvpd[pdeIndex] & PTE_P)) {
					continue;
				}
				
				for (int l = 0; l < NPTENTRIES; l++) {
					uint64_t pn = pdeIndex * NPTENTRIES + l;
					uintptr_t va = (uintptr_t)(pn << PGSHIFT);
					
					if (va >= UTOP) {
						goto done;
					}
					
					if (va >= (UXSTACKTOP - PGSIZE) && va < UXSTACKTOP) {
						continue;
					}
					
					if (!(uvpt[pn] & PTE_P)) {
						continue;
					}
		
					r = duppage(envid, pn);
					if (r < 0) {
						panic("duppage failed:  %e", r);
					}
				}
			}
		}
	}
done:
	
	r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W);
	if (r < 0) {
		panic("sys_page_alloc for child exception stack failed: %e", r);
	}
	
	r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if (r < 0) {
		panic("sys_env_set_pgfault_upcall failed:  %e", r);
	}
	
	r = sys_env_set_status(envid, ENV_RUNNABLE);
	if (r < 0) {
		panic("sys_env_set_status failed:  %e", r);
	}
	
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
