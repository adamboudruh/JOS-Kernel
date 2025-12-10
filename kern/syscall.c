/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, s, len, PTE_U|PTE_P);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.
	/*
	Steps:
	- declare pointer to struct Env *e
	- call env_alloc(&e, curenv ? curenv->env_id, 0);
	- status of child env (e->env_status) must be set to ENV_NOT_RUNNABLE
	- copy registers from parent: e->env_tf = curenv->env_tf;
	- set return value for child env as 0: e->env_tf.tf_regs.reg_rax = 0;
	- return e->env_id;
	*/
	// LAB 4: Your code here.
	struct Env *e;
	int r = env_alloc(&e, curenv ? curenv->env_id : 0);
	
	if (r < 0) {
		return r;
	}
	
	e->env_status = ENV_NOT_RUNNABLE;
	e->env_tf = curenv->env_tf;
	e->env_tf.tf_regs.reg_rax = 0;
	
	return e->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.
	/*
	Steps:
	- if status is not ENV_RUNNABLE or ENV_NOT_RUNNABLE return -E_INVAL
	- use envid2env() that converts envid_t to a pointer to struct Env; last param to this func is non zero
		- if that returns negative, return -E_BAD_ENV
	- If good so far, assign status provided as a param to environment obtained with envid2env and return 0
	*/
	// LAB 4: Your code here.
	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
		return -E_INVAL;
	}
	
	struct Env *e;
	int r = envid2env(envid, &e, 1);
	if (r < 0) {
		return -E_BAD_ENV;
	}
	
	e->env_status = status;
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *e = NULL;
	if (envid2env(envid, &e, 1) < 0) {
		return -E_BAD_ENV;
	}
	e->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!
	/*
	Steps:
	- use envid2env() to convert again; if negative return -E_BAD_ENV
	- check va is page aligned: if((uintptr_t)va & 0xFFF) return -E_INVAL;
	- if va >= UTOP, else return -E_INVAL
	- check that perm invludes bits in the PTE_SYSCALL and no other with:
		if ((perm & 0xFFF) & (~PTE_SYSCALL)) return -E_INVAL;
	- after validation, use page_alloc(ALLOC_ZERO) to get a page as struct PageInfo *p
		- if NULL, return -E_NO_MEM
	- use page_insert() to map page:
		page_insert(env->env_pml4e, p, va, perm | PTE_U | PTE_P);
	- if error call page_free(p) and return error, otherwise return 0
	*/
	// LAB 4: Your code here.
	struct Env *e;
	int r = envid2env(envid, &e, 1);
	if (r < 0) {
		return -E_BAD_ENV;
	}
	if (((uintptr_t)va & 0xFFF) != 0) {
		return -E_INVAL;
	}
	if ((uintptr_t)va >= UTOP) {
		return -E_INVAL;
	}
	if ((perm & 0xFFF) & (~PTE_SYSCALL)) {
		return -E_INVAL;
	}
	struct PageInfo *p = page_alloc(ALLOC_ZERO);
	if (p == NULL) {
		return -E_NO_MEM;
	}
	r = page_insert(e->env_pml4e, p, va, perm | PTE_U | PTE_P);
	if (r < 0) {
		page_free(p);
		return -E_NO_MEM;
	}
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.
	/*
	Steps:
	- validate input params like in sys_page_alloc
		- use envid2env() for both src and dst envs; if negative return -E_BAD_ENV
		- check that vas are page aligned with: 
		  if ((uintptr_t)srcva % PGSIZE != 0) || ((uintptr_t)dstva % PGSIZE != 0) return -E_INVAL;
		- check that vas < UTOP; else return -E_INVAL
		- check perm bits like in sys_page_alloc: 
		  if ((perm & 0xFFF) & (~PTE_SYSCALL)) return -E_INVAL;
	- after vals, use page_lookup() to get struct PageInfo *p from srcva
		- if NULL, return -E_INVAL
		- check if perm has PTE_W but page is read only; if so return -E_INVAL
	- use page_insert() function to map page p to dstva with req permissions
		- if error, return it; else return 0
	*/
	// LAB 4: Your code here.
	struct Env *srcenv, *dstenv;
    struct PageInfo *p;
    pte_t *pte;
    int r;
    
    // validation
    
    r = envid2env(srcenvid, &srcenv, 1);
    if (r < 0) {
        return -E_BAD_ENV;
    }
    
    r = envid2env(dstenvid, &dstenv, 1);
    if (r < 0) {
        return -E_BAD_ENV;
    }
    
    if ((uintptr_t)srcva % PGSIZE != 0 || (uintptr_t)dstva % PGSIZE != 0) {
        return -E_INVAL;
    }
    
    if ((uintptr_t)srcva >= UTOP || (uintptr_t)dstva >= UTOP) {
        return -E_INVAL;
    }
    
    if (perm & ~PTE_SYSCALL) {
        return -E_INVAL;
    }
    
    if ((perm & (PTE_U | PTE_P)) != (PTE_U | PTE_P)) {
        return -E_INVAL;
    }

    p = page_lookup(srcenv->env_pml4e, srcva, &pte);
    if (p == NULL) {
        return -E_INVAL;
    }
    
    if ((perm & PTE_W) && !(*pte & PTE_W)) {
        return -E_INVAL;
    }

    r = page_insert(dstenv->env_pml4e, p, dstva, perm);
    if (r < 0) {
        return r;
    }
    
    return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().
	/*
	Steps
	- once again, validate all the input stuff
		- use envid2env(); if negative return -E_BAD_ENV
		- check va is page aligned; if not return -E_INVAL
		- check va < UTOP; if not return -E_INVAL
	- use page_remove() to unmap the page and return 0
	*/
	// LAB 4: Your code here.
	struct Env *e;
	int r = envid2env(envid, &e, 1);
	if (r < 0) {
		return -E_BAD_ENV;
	}
	if ((uintptr_t)va % PGSIZE != 0) {
		return -E_INVAL;
	}
	if ((uintptr_t)va >= UTOP) {
		return -E_INVAL;
	}
	page_remove(e->env_pml4e, va);
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
/*
- checks if env 2 is waiting for a message; if so:
	- copy message to env_ipc_value
	- set env_ipc_from to sender's environment
	- change status of env 2 to ENV_RUNNABLE

- when env 2 is runnable again, it may obtain the message from the env_ipc_value and know who sent it from the env_ipc_from field
- if sending message larger than word, allocate a page
- env 1 places the address of allocated page in var env_ipc_dstva
- env_ipc_value should also contain the size of the message

Steps
- params:
	- envid is id of receiver env
	- message or size of message in value
	- void pointer srcva; if that pointer has address under UTOP, means that function is sending a page
	- perm is permissions for page mapping
- validate existence of receiver env by getting struct Env *e with received envid and envid2env(); if negative return error code
- check that env_icp_receving is set to 1; if not return -E_IPC_NOT_RECV
- if srcva < UTOP return -E_INVAL and not page aligned, return -E_INVAL
- if srcva < UTOP check perm, make sure no PTE_U, PTE_P, PTE_SYSCALL bits are missing; if so return -E_INVAL
- if srcva < UTOP uobtain struct PageInfo *pp with:
	pp = page_lookup(curenv->env_pml4e, srcva, &pte);
- now ppte is pte_t* ppte
- if pp or ppte are NULL return -E_INVAL
- if (perm & PTE_W) but !(*ppte & PTE_W) return -E_INVAL
- call page_insert() to insert page pointed to by pp using perm; if error return it
- finally set env_ipc_value in receiving environment to value param
- set env_inc_from var in recv env to curenv->env_id
- set env_status in recv env to ENV_RUNNABLE
- also set env_ipc_perm and env_ipc_recving to zero
- also also set reg_rax in recv env's TrapFrame to 0: e->env_tf.tf_regs.reg_rax = 0;
return 0
*/
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	struct Env *e;
	int r;
	
	r = envid2env(envid, &e, 0);
	if (r < 0) {
		return -E_BAD_ENV;
	}
	
	if (! e->env_ipc_recving) {
		return -E_IPC_NOT_RECV;
	}
	
	if ((uintptr_t)srcva < UTOP) {
		if ((uintptr_t)srcva % PGSIZE != 0) {
			return -E_INVAL;
		}
		
		if (!(perm & PTE_U) || !(perm & PTE_P)) {
			return -E_INVAL;
		}
		if (perm & ~PTE_SYSCALL) {
			return -E_INVAL;
		}
		
		pte_t *pte;
		struct PageInfo *pp = page_lookup(curenv->env_pml4e, srcva, &pte);
		if (!pp) {
			return -E_INVAL;
		}
		
		if ((perm & PTE_W) && !(*pte & PTE_W)) {
			return -E_INVAL;
		}
		
		if ((uintptr_t)e->env_ipc_dstva < UTOP) {
			r = page_insert(e->env_pml4e, pp, e->env_ipc_dstva, perm);
			if (r < 0) {
				return -E_NO_MEM;
			}
			e->env_ipc_perm = perm;
		} else {
			e->env_ipc_perm = 0;
		}
	} else {
		e->env_ipc_perm = 0;
	}
	
	// update target env
	e->env_ipc_recving = 0;
	e->env_ipc_from = curenv->env_id;
	e->env_ipc_value = value;
	e->env_tf. tf_regs. reg_rax = 0;
	e->env_status = ENV_RUNNABLE;
	
	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
/*
- sets env_ipc_recving to 1
- makes env wait by changing its status to ENV_NOT_RUNNABLE
- sets reg_eax = 0 record in TrapFrame
- calls sched_yield() to schedule another env in cpu

Steps:
- receives an address dstva where a message can be found. if address not aligned return -E_INVAL
	- use % PGSIZE
- if address in dstva is smaller than UTOP: curenv->env_ipc_dstva = dstva;
- must also set curenv->env_ipc_recving = 1;
- set curenv->env_status = ENV_NOT_RUNNABLE;
- set curenv->env_tf.tf_regs.reg_rax = 0;
- finally call sched_yield() to deschedule current env, then return 0
*/
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	if ((uintptr_t)dstva < UTOP) {
		if ((uintptr_t)dstva % PGSIZE != 0) return -E_INVAL;
		curenv->env_ipc_dstva = dstva;
	} else {
		curenv->env_ipc_dstva = (void *)UTOP;
	}
	
	curenv->env_ipc_recving = 1;
	curenv->env_status = ENV_NOT_RUNNABLE;
	sched_yield();
	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
/*
- receives 5 unsigned ints
- uses syscallno to decide what to do:
	SYS_cputs = 0,
	SYS_cgetc,
	SYS_getenvid,
	SYS_env_destroy
	NSYSCALLS (not actually a call number)
- if call number is SYS_cputs, call sys_cputs((const char *)a1, (size_t)a2);
- Do same for all other syscalls
*/
int64_t
syscall(uint64_t syscallno, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	switch (syscallno) {

	case SYS_cputs:
        sys_cputs((const char *)a1, (size_t)a2);
        return 0;
    
    case SYS_cgetc: 
        return sys_cgetc();
    
    case SYS_getenvid:
        return sys_getenvid();
    
    case SYS_env_destroy: 
        return sys_env_destroy((envid_t)a1);
	
	// activating functions for exercise 7
	case SYS_yield:
	    sys_yield();
		return 0;
	case SYS_exofork:
	    return sys_exofork();
	case SYS_env_set_status:
		return sys_env_set_status((envid_t)a1, (int)a2);
	case SYS_page_alloc:
		return sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
	case SYS_page_map:
		return sys_page_map((envid_t)a1, (void *)a2, (envid_t)a3, (void *)a4, (int)a5);
	case SYS_page_unmap:
		return sys_page_unmap((envid_t)a1, (void *)a2);	
	case SYS_env_set_pgfault_upcall:
		return sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
	case SYS_ipc_try_send:
		return sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void *)a3, (unsigned)a4);
	case SYS_ipc_recv:
		return sys_ipc_recv((void *)a1);

	default:
		return -E_INVAL;
	}
}

