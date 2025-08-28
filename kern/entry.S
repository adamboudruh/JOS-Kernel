/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>

# Shift Right Logical 
#define SRL(val, shamt)		(((val) >> (shamt)) & ~(-1 << (32 - (shamt))))


###################################################################
# The kernel (this code) is linked at address ~(KERNBASE + 1 Meg), 
# but the bootloader loads it at address ~1 Meg.
#	
# RELOC(x) maps a symbol x from its link address to its actual
# location in physical memory (its load address).	 
###################################################################

#define	RELOC(x) ((x) - KERNBASE)

#define MULTIBOOT_HEADER_MAGIC (0x1BADB002)
#define MULTIBOOT_HEADER_FLAGS (0)
#define CHECKSUM (-(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS))

###################################################################
# entry point
###################################################################

.text

# The Multiboot header
.align 4
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long CHECKSUM

# '_start' specifies the ELF entry point.  Since we haven't set up
# virtual memory when the bootloader enters this code, we need the
# bootloader to jump to the *physical* address of the entry point.
.globl		_start
_start = RELOC(entry)

.globl entry
entry:
	.code64
	cli
	cld

	movabs	$(RELOC(entry_pml4e)), %rax
	movq	%rax, %cr3

    movabs $relocated, %rax

    jmp *%rax


relocated:
	.code64 

	# Enable SSE
	mov    %cr0, %rax
	and    $~(1<<2), %rax
	or     $(1<<1), %rax
	mov    %rax, %cr0

	mov    %cr4, %rax
	or     $(1<<9 | 1<<10), %rax
	mov    %rax, %cr4
	
	# Clear the frame pointer register (EBP)
	# so that once we get into debugging C code,
	# stack backtraces will be terminated properly.
	movq	$0x0,%rbp			# nuke frame pointer

	# Set the stack pointer
	movabs	$(bootstacktop),%rsp

	# now to C code
	call	i386_init

	# Should never get here, but in case we do, just spin.
spin:	jmp	spin

.data
###################################################################
# boot stack
###################################################################
	.p2align	PGSHIFT		# force page alignment
	.globl		bootstack
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop   
bootstacktop:

.data
# Kernel gdt
    .p2align 3
    .globl kernel_64
gdt_64:
    SEG_NULL
    SEG64(STA_X|STA_R,0x0,0xffffffff)    #64 bit CS
    SEG64(STA_R|STA_W,0x0,0xffffffff)    #64 bit DS
    SEG64USER(STA_X|STA_R,0x0,0xffffffff) #64 bit USER CS
    SEG64USER(STA_R|STA_W,0x0,0xffffffff) # USER data
    .quad   0x0080890000000000  /* TS descriptor */ 
    .quad   0x0000000000000000 /* TS continued */

     .globl gdtdesc_64
gdtdesc_64:
    .word 0x37 
    .quad gdt_64
