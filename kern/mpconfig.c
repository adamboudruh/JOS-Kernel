// Search for and parse the multiprocessor configuration table
// See http://developer.intel.com/design/pentium/datashts/24201606.pdf

#include <inc/types.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/env.h>
#include <kern/cpu.h>
#include <kern/pmap.h>

struct CpuInfo cpus[NCPU];
struct CpuInfo *bootcpu;
int ismp;
int ncpu;

// Per-CPU kernel stacks
unsigned char percpu_kstacks[NCPU][KSTKSIZE]
__attribute__ ((aligned(PGSIZE)));


// See ACPI Specification

//https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html#multiple-apic-description-table-madt
typedef struct {
	uint8_t Type;
	uint8_t Length;
	union{
		struct __attribute__ ((packed)){//0 Processor Local APIC Structure
			uint8_t ACPI_CPU_ID;//0
			uint8_t APIC_ID;//8
			uint32_t LAPIC_FLAGS;
		}LAPIC;
		struct __attribute__ ((packed)){//1  I/O APIC Structure
			uint8_t IO_APIC_ID;
			uint8_t RESERVED;//0
			uint32_t IOAPIC_ADDRESS;
			uint32_t GSI_BASE;
		}IOAPIC;
		struct __attribute__ ((packed)){//2 Interrupt Source Override Structure
			uint8_t ISA_BUS;
			uint8_t IRQ_SOURCE;
			uint32_t ISA_GSI;
			uint16_t ISA_FLAGS;
		}ISO;
		struct __attribute__ ((packed)){//3 Non-Maskable Interrupt (NMI) Source Structure
			uint16_t NMI_FLAGS;
			uint32_t NMI_GSI;
		}NMI;
		struct __attribute__ ((packed)){//4 Local APIC NMI Structure
			uint8_t ACPI_Processor_UID;
			uint16_t LAPIC_NMI_FLAGS;
			uint8_t LAPIC_LINT;
		}LNMI;
		struct __attribute__ ((packed)){//5 Local APIC Address Override Structure
			uint16_t RESERVED2;
			uint64_t LAPIC_ADDRESS;
		}LAPIC_OVERRIDE;
		struct __attribute__ ((packed)){//6 I/O SAPIC Structure
			uint8_t IO_SAPIC_ID;
			uint8_t RESERVED3;
			uint32_t SAPIC_GSI_BASE;
			uint64_t IOSAPIC_ADDRESS;
		}IOSAPIC;
		struct __attribute__ ((packed)){//7 Local SAPIC Structure
			uint8_t ACPI_Processor_ID;
			uint8_t Local_SAPIC_ID;
			uint8_t Local_SAPIC_EID;
			uint8_t RESERVED4;
			uint16_t RESERVED5;
			uint32_t LSAPIC_FLAGS;
			uint64_t LSAPIC_ADDRESS;
		}LSAPIC;
		struct __attribute__ ((packed)){//8 Platform Interrupt Source Structure
			uint16_t MPS_INTI_FLAGS;
			uint8_t INT_TYPE;
			uint8_t DEST_PROCESSOR_ID;
			uint8_t DEST_PROCESSOR_EID;
			uint8_t IO_SAPIC_VECTOR;
			uint32_t PIS_GSI;
			uint32_t PIS_FLAGS;
		}PLIS;
	}TABLE;
}__attribute__ ((packed)) MADT_ENTRY;


typedef struct {
	char Signature[8];
	uint8_t Checksum;
	char OEMID[6];
	uint8_t Revision;
	uint32_t RsdtAddress;
} __attribute__ ((packed)) RSDP_t;

typedef struct {
	char Signature[8];
	uint8_t Checksum;
	char OEMID[6];
	uint8_t Revision;
	uint32_t RsdtAddress;      // deprecated since version 2.0

	uint32_t Length;
	uint64_t XsdtAddress;
	uint8_t ExtendedChecksum;
	uint8_t reserved[3];
} __attribute__ ((packed)) XSDP_t;

typedef struct {
	char Signature[4];
	uint32_t Length;
	uint8_t Revision;
	uint8_t Checksum;
	char OEMID[6];
	char OEMTableID[8];
	uint32_t OEMRevision;
	uint32_t CreatorID;
	uint32_t CreatorRevision;
}__attribute__ ((packed)) ACPISDTHeader;

typedef struct {
	ACPISDTHeader h;
	uint64_t PointerToOtherSDT[];
}__attribute__ ((packed)) XSDT;

typedef struct {
	ACPISDTHeader h;
	uint32_t PointerToOtherSDT[];
}__attribute__ ((packed)) RSDT;

typedef struct {
	ACPISDTHeader h;
	uint32_t LAPIC_ADDRESS;
	uint32_t Flags;
}__attribute__ ((packed)) MADT;

void* findSDT(void *RootSDT, const char* SDT){
	XSDT *xsdt = (XSDT *) RootSDT;

	int entries = (xsdt->h.Length - sizeof(ACPISDTHeader)) / 8;

	for (int i = 0; i < entries; i++){
		ACPISDTHeader *h = (ACPISDTHeader *)KADDR(xsdt->PointerToOtherSDT[i]);
		if (!strncmp(h->Signature, SDT, 4))
			return (void *) h;
	}
 
	// No SDT found
	return NULL;
}

void
mp_init(void* r)
{

	XSDP_t* rsdp=KADDR((uintptr_t)r);
	cprintf("RSDP:0x%x\nOEMID:", rsdp);
	for (int i = 0; i < 6; ++i){
		cprintf("%c",rsdp->OEMID[i]);
	}
	cprintf("\n");

	cprintf("version:%d\n",rsdp->Revision);

	if(rsdp->Revision==0){
		cprintf("Found acpi 1.0 table.\n Unimplemented.");
		// return;
	}
	else if(rsdp->Revision==2){
		cprintf("Found acpi 2.0 table.\n");
	}

	XSDT *xsdt = (XSDT *) KADDR(rsdp->XsdtAddress);

	MADT* madt = findSDT(xsdt,"APIC");
	lapicaddr = (physaddr_t)madt->LAPIC_ADDRESS;

	bootcpu = &cpus[0];

	ismp = 1;

	void* end = incptr(madt, madt->h.Length);

	for(MADT_ENTRY*entry =(MADT_ENTRY*)(madt+1); (void*)entry < end; entry=incptr(entry,entry->Length)) {
		switch(entry->Type) {
			case 0:// found LAPIC
				if(entry->TABLE.LAPIC.LAPIC_FLAGS & 1){//core enabled
					if (ncpu < NCPU) {
						cpus[ncpu].cpu_id = ncpu;
						ncpu++;
					} else {
						cprintf("SMP: too many CPUs, CPU %d disabled\n",
							entry->TABLE.LAPIC.APIC_ID);
					}
				}
				cprintf("found cpu:%x\n",entry->TABLE.LAPIC.APIC_ID);
				break; 
		}
	}


	bootcpu->cpu_status = CPU_STARTED;
	if (!ismp) {
		// Didn't like what we found; fall back to no MP.
		ncpu = 1;
		lapicaddr = 0;
		cprintf("SMP: configuration not found, SMP disabled\n");
		return;
	}
	cprintf("SMP: CPU %d found %d CPU(s)\n", bootcpu->cpu_id,  ncpu);

	if (true) {//acpi I think requires this
		// [MP 3.2.6.1] If the hardware implements PIC mode,
		// switch to getting interrupts from the LAPIC.
		cprintf("SMP: Setting IMCR to switch from PIC mode to symmetric I/O mode\n");
		outb(0x22, 0x70);   // Select IMCR
		outb(0x23, inb(0x23) | 1);  // Mask external interrupts.
	}
}
