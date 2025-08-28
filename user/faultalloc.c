// test user-level fault handler -- alloc pages to fix faults

#include <inc/lib.h>

void
handler(struct UTrapframe *utf)
{
	int r;
	void *addr = (void*)utf->utf_fault_va;

	cprintf("fault %x\n", addr);
	if ((r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE),
				PTE_P|PTE_U|PTE_W)) < 0)
		panic("allocating at %x in page fault handler: %e", addr, r);
	snprintf((char*) addr, 100, "this string was faulted in at %x", addr);
}

void
umain(int argc, char **argv)
{
    unsigned long value = 0x1234567812345678; 
    __asm__ volatile (
        "mov %0, %%r9\n"
        "mov %0, %%r10\n"
        "mov %0, %%r11\n"
        "mov %0, %%r12\n"
        "mov %0, %%r13\n"
        "mov %0, %%r14\n"
        : // No output operands
        : "r" (value) // Input operand
        : // Clobbers
    );	
    set_pgfault_handler(handler);
	cprintf("%s\n", (char*)0x6eadBeef);
	cprintf("%s\n", (char*)0x6afeBffe);
}
