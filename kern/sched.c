#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.
	/*
	Tips
	- curenv->env_id to get current env's id
	- loop to search for next env to run (goes over envs array using % NENV)
		- Begin at env after current one in envs
		- if curenv is not NULL the next env is envs[(ENVX(curenv->env_id) + 1) % NENV]
		- if curenv == NULL, assume curenv is envs[0] and next one would be envs[ENVX(1) % NENV]
		- loop stops when a runnable env is found or we looped back to starting env
		- then call env_run() with that env, and if not run curenv again if ENV_RUNNING

	*/
	// LAB 4: Your code here.

	int start;
    int cur;
    int i;

    if (curenv != NULL) {
        start = ENVX(curenv->env_id);
    } else {
        start = 0;
    }

    for (i = 0; i < NENV; i++) {
        cur = (start + 1 + i) % NENV;
        
        if (envs[cur].env_status == ENV_RUNNABLE) {
            env_run(&envs[cur]);
            // env_run doesn't return
        }
    }

    if (curenv != NULL && curenv->env_status == ENV_RUNNING) {
        env_run(curenv);
    }

	// sched_halt never returns
	sched_halt();
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pml4));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movq $0, %%rbp\n"
		"movq %0, %%rsp\n"
		"pushq $0\n"
		"pushq $0\n"
        // LAB 4:
		// Uncomment the following line after completing exercise 13
		"sti\n"
		"1:\n"
		"hlt\n"
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.RSP[0]));
}

