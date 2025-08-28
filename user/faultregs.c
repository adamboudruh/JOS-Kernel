// test register restore on user-level page fault return

#include <inc/lib.h>

struct regs
{
	struct PushRegs regs;
	uintptr_t rip;
	uint64_t eflags;
	uintptr_t rsp;
};


#define SAVE_REGS(base) \
	"\tmovq %%r15, 0x0("base")\n" \
	"\tmovq %%r14, 0x8("base")\n" \
	"\tmovq %%r13, 0x10("base")\n" \
	"\tmovq %%r12, 0x18("base")\n" \
	"\tmovq %%r11, 0x20("base")\n" \
	"\tmovq %%r10, 0x28("base")\n" \
	"\tmovq %%r9, 0x30("base")\n" \
	"\tmovq %%r8, 0x38("base")\n" \
	"\tmovq %%rsi, 0x40("base")\n" \
	"\tmovq %%rdi, 0x48("base")\n" \
	"\tmovq %%rbp, 0x50("base")\n" \
	"\tmovq %%rdx, 0x58("base")\n" \
	"\tmovq %%rcx, 0x60("base")\n" \
	"\tmovq %%rbx, 0x68("base")\n" \
	"\tmovq %%rax, 0x70("base")\n" \
	"\tmovq %%rsp, 0x88("base")\n"

#define LOAD_REGS(base) \
	"\tmovq 0x0("base"), %%r15\n" \
	"\tmovq 0x8("base"), %%r14\n" \
	"\tmovq 0x10("base"), %%r13\n" \
	"\tmovq 0x18("base"), %%r12 \n" \
	"\tmovq 0x20("base"), %%r11\n" \
	"\tmovq 0x28("base"), %%r10\n" \
	"\tmovq 0x30("base"), %%r9\n" \
	"\tmovq 0x38("base"), %%r8\n" \
	"\tmovq 0x40("base"), %%rsi\n" \
	"\tmovq 0x48("base"), %%rdi\n" \
	"\tmovq 0x50("base"), %%rbp\n" \
	"\tmovq 0x58("base"), %%rdx\n" \
	"\tmovq 0x60("base"), %%rcx\n" \
	"\tmovq 0x68("base"), %%rbx\n" \
	"\tmovq 0x70("base"), %%rax\n" \
	"\tmovq 0x88("base"), %%rsp\n"

static struct regs before, during, after;

static void
check_regs(struct regs* a, const char *an, struct regs* b, const char *bn,
	   const char *testname)
{
	int mismatch = 0;

	cprintf("%-6s %-8s %-8s\n", "", an, bn);

#define CHECK(name, field)						\
	do {								\
		cprintf("%-6s %016x %016x ", #name, a->field, b->field);	\
		if (a->field == b->field)				\
			cprintf("OK\n");				\
		else {							\
			cprintf("MISMATCH\n");				\
			mismatch = 1;					\
		}							\
	} while (0)

	CHECK(r15, regs.reg_r15);
	CHECK(r14, regs.reg_r14);
	CHECK(r13, regs.reg_r13);
	CHECK(r12, regs.reg_r12);
	CHECK(r11, regs.reg_r11);
	CHECK(r10, regs.reg_r10);
	CHECK(r9, regs.reg_r9);
	CHECK(r8, regs.reg_r8);

	CHECK(rbp, regs.reg_rbp);
	CHECK(rdi, regs.reg_rdi);
	CHECK(rsi, regs.reg_rsi);
	CHECK(rdx, regs.reg_rdx);
	CHECK(rcx, regs.reg_rcx);
	CHECK(rbx, regs.reg_rbx);
	CHECK(rax, regs.reg_rax);

	CHECK(rip, rip);
	CHECK(eflags, eflags);
	CHECK(rsp, rsp);

#undef CHECK

	cprintf("Registers %s ", testname);
	if (!mismatch)
		cprintf("OK\n");
	else
		cprintf("MISMATCH\n");
}

static void
pgfault(struct UTrapframe *utf)
{
	int r;

	if (utf->utf_fault_va != (uint64_t)UTEMP)
		panic("pgfault expected at UTEMP, got 0x%016x (rip %016x)",
		      utf->utf_fault_va, utf->utf_rip);

	// Check registers in UTrapframe
	during.regs = utf->utf_regs;
	during.rip = utf->utf_rip;
	during.eflags = utf->utf_eflags & ~FL_RF;
	during.rsp = utf->utf_rsp;
	check_regs(&before, "before", &during, "during", "in UTrapframe");

	// Map UTEMP so the write succeeds
	if ((r = sys_page_alloc(0, UTEMP, PTE_U|PTE_P|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
}

void
umain(int argc, char **argv)
{
	set_pgfault_handler(pgfault);

	asm volatile(
		// Light up eflags to catch more errors
		"\tpushq %%rax\n"
		"\tpushfq\n"
		"\tpopq %%rax\n"
		"\torq $0x8d4, %%rax\n"
		"\tpushq %%rax\n"
		"\tpopfq\n"

		// Save before registers directly into the 'before' struct
		// eflags
		"\tmovq %%rax, 0x80(%0)\n"        // Save modified eflags into 'before.eflags'
		// rip
		"\tleaq 0f, %%rax\n"
		"\tmovq %%rax, 0x78(%0)\n"        // Save rip into 'before.rip'
		"\tpopq %%rax\n"
		// others
		SAVE_REGS("%0")

		// Fault at UTEMP
		"\t0: movl $42, 0x400000\n"

		// Save after registers directly into the 'after' struct (except rip and eflags)
		SAVE_REGS("%1")

		// Restore registers (except rip and eflags)
		LOAD_REGS("%1")

		// Save after eflags (now that stack is back)
		"\tpushq %%rax\n"
		"\tpushfq\n"
		"\tpopq %%rax\n"
		"\tmovq %%rax, 0x80(%1)\n"        // Save eflags into 'after.eflags'
		"\tpopq %%rax\n"

		// Pop the addresses pushed at the beginning to balance the stack
		: : "r" (&before), "r" (&after) : "memory", "cc", "r15", "rax");

	// Check UTEMP to roughly determine that EIP was restored
	// correctly (of course, we probably wouldn't get this far if
	// it weren't)
	if (*(int*)UTEMP != 42)
		cprintf("RIP after page-fault MISMATCH\n");
	after.rip = before.rip;

	check_regs(&before, "before", &after, "after", "after page-fault");
}
//one more push
