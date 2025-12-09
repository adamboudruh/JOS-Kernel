/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushq $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushq $0;							\
	pushq $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

TRAPHANDLER_NOEC(t_divide, T_DIVIDE);
TRAPHANDLER_NOEC(t_debug, T_DEBUG);
TRAPHANDLER_NOEC(t_nmi, T_NMI);
TRAPHANDLER_NOEC(t_brkpt, T_BRKPT);
TRAPHANDLER_NOEC(t_oflow, T_OFLOW);
TRAPHANDLER_NOEC(t_bound, T_BOUND);
TRAPHANDLER_NOEC(t_illop, T_ILLOP);
TRAPHANDLER_NOEC(t_device, T_DEVICE);
TRAPHANDLER(t_dblflt, T_DBLFLT);
TRAPHANDLER(t_tss, T_TSS);
TRAPHANDLER(t_segnp, T_SEGNP);
TRAPHANDLER(t_stack, T_STACK);
TRAPHANDLER(t_gpflt, T_GPFLT);
TRAPHANDLER(t_pgflt, T_PGFLT);
TRAPHANDLER_NOEC(t_fperr, T_FPERR);
TRAPHANDLER(t_align, T_ALIGN);
TRAPHANDLER_NOEC(t_mchk, T_MCHK);
TRAPHANDLER_NOEC(t_simderr, T_SIMDERR);
TRAPHANDLER_NOEC(t_syscall, T_SYSCALL);
TRAPHANDLER_NOEC(t_default, T_DEFAULT);

/* HINT 1 : TRAPHANDLER_NOEC(t_divide, T_DIVIDE);
//          Do something like this if there is no error code for the trap
// HINT 2 : TRAPHANDLER(t_dblflt, T_DBLFLT);
//          Do something like this if the trap includes an error code..
// HINT 3 : READ Intel's manual to check if the trap includes an error code
//          or not... */


/*
 * Lab 3: Your code here for _alltraps
 Notes:
 - must fill Program Stack with contents of current registers in proper order
 - rsp register is always pointing to top of stack
 - we can add space on the stack to store registers with subq command
 - 15 registers total, 8 bytes long each, so subtract 15*8 = 120 bytes with `subq $120, %rsp`
 - after this, we can use movq to insert register contents
 - i.e. movq %rax,112(%rsp); movq %rbx.104(%rsp); etc.
 - order: rax, rbx, rcx, rdx, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15
 - after all registers stored, end with `movq %rsp, %rdi; call trap;`
 */

 _alltraps:
    
    subq $120, %rsp              # Allocate space for 15 registers
    movq %rax, 112(%rsp)
    movq %rbx, 104(%rsp)
    movq %rcx, 96(%rsp)
    movq %rdx, 88(%rsp)
    movq %rbp, 80(%rsp)
    movq %rsi, 72(%rsp)
    movq %rdi, 64(%rsp)
    movq %r8,  56(%rsp)
    movq %r9,  48(%rsp)
    movq %r10, 40(%rsp)
    movq %r11, 32(%rsp)
    movq %r12, 24(%rsp)
    movq %r13, 16(%rsp)
    movq %r14, 8(%rsp)
    movq %r15, 0(%rsp)
    
    movq %rsp, %rdi
    call trap

