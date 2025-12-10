#include <inc/mmu.h>
#include <inc/memlayout.h>

// Page fault upcall entrypoint.

// This is where we ask the kernel to redirect us to whenever we cause
// a page fault in user space (see the call to sys_set_pgfault_handler
// in pgfault.c).
//
// When a page fault actually occurs, the kernel switches our ESP to
// point to the user exception stack if we're not already on the user
// exception stack, and then it pushes a UTrapframe onto our user
// exception stack:
//
//	trap-time rsp
//	trap-time rflags
//	trap-time rip
//	utf_regs.reg_rax
//	...
//	utf_regs.reg_r14
//	utf_regs.reg_r15
//	utf_err (error code)
//	utf_fault_va            <-- %rsp
//
// If this is a recursive fault, the kernel will reserve for us a
// blank word above the trap-time esp for scratch work when we unwind
// the recursive call.
//
// We then have call up to the appropriate page fault handler in C
// code, pointed to by the global variable '_pgfault_handler'.

.text
.globl _pgfault_upcall
_pgfault_upcall:
	// Call the C page fault handler.
	movq  %rsp,%rdi		// function argument: pointer to UTF
	movabs _pgfault_handler, %rax
	call *%rax
	
	/* Now the C page fault handler has returned and you must return
	// to the trap time state.
	// Push trap-time %eip onto the trap-time stack.
	//
	// Explanation:
	//   We must prepare the trap-time stack for our eventual return to
	//   re-execute the instruction that faulted.
	//   Unfortunately, we can't return directly from the exception stack:
	//   We can't call 'jmp', since that requires that we load the address
	//   into a register, and all registers must have their trap-time
	//   values after the return.
	//   We can't call 'ret' from the exception stack either, since if we
	//   did, %rsp would have the wrong value.
	//   So instead, we push the trap-time %rip onto the *trap-time* stack!
	//   Below we'll switch to that stack and call 'ret', which will
	//   restore %rip to its pre-fault value.
	//
	//   In the case of a recursive fault on the exception stack,
	//   note that the word we're pushing now will fit in the
	//   blank word that the kernel reserved for us.
	//
	// Throughout the remaining code, think carefully about what
	// registers are available for intermediate calculations.  You
	// may find that you have to rearrange your code in non-obvious
	// ways as registers become unavailable as scratch space.
	//
	// LAB 4: Your code here. (Hint. You need to modify the trap time rsp to make room for the return rip ) */

	movq  152(%rsp), %rax
	subq $8, %rax
	movq %rax, 152(%rsp)

	movq 136(%rsp), %rbx
	movq %rbx, (%rax)

	/* Restore the trap-time registers.  After you do this, you
	// can no longer modify any general-purpose registers.
	// LAB 4: Your code here. (Hint, the oposite of _alltraps) */

	addq $16, %rsp
	
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax

	addq $8, %rsp

	/* Restore rflags from the stack.  After you do this, you can
	// no longer use arithmetic operations or anything else that
	// modifies rflags.
	// LAB 4: Your code here. */

	popfq

	/* Switch back to the adjusted trap-time stack.
	// LAB 4: Your code here. */

	popq %rsp

	/* Return to re-execute the instruction that faulted.
	// LAB 4: Your code here. */
	ret