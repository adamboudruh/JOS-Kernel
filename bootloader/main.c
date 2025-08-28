

/*
Global Variable Description
*BS, gBS efi_boot_services_t, pointer to the Boot Time Services
*RT, gRT efi_runtime_t, pointer to the Runtime Services
*ST, gST efi_system_table_t, pointer to the UEFI System Table
IM efi_handle_t of your Loaded Image
*/

#include <uefi.h>
#include "elf.h"


typedef struct {
	efi_memory_descriptor_t* mem_map;
	uintn_t map_size;
	uintn_t map_desc_size;
	void* rsdp;
}bootinfo;

uintn_t get_mem_map(bootinfo* info){
	uintn_t EfiMemoryMapSize = 0;
	efi_memory_descriptor_t* descriptors = NULL;
	uintn_t EfiMapKey = 0;
	uintn_t EfiDescriptorSize = 0;
	uint32_t descriptorVersion = 0;
	
	efi_status_t status;
	
	while ((status = BS->GetMemoryMap(&EfiMemoryMapSize, descriptors, &EfiMapKey, &EfiDescriptorSize, &descriptorVersion)) == EFI_BUFFER_TOO_SMALL){
	    EfiMemoryMapSize += EfiDescriptorSize * 10;
	    if(descriptors)
	    	free(descriptors);
	    descriptors = (efi_memory_descriptor_t*)malloc(EfiMemoryMapSize);
	}

	if (EFI_ERROR(status)){
	    printf("Failed to retrieve the EFI memory map: %x\n", status);
	    return -1;
	}

	efi_memory_descriptor_t* EfiMemoryMap = (efi_memory_descriptor_t*) descriptors;
	if(info){
		info->mem_map=EfiMemoryMap;
		info->map_size=EfiMemoryMapSize;
		info->map_desc_size=EfiDescriptorSize;
	}
	return EfiMapKey;
}

void* find_rsdp(){
	void* rsdp = NULL; 

	efi_configuration_table_t* configTable = ST->ConfigurationTable;
	
	efi_guid_t acpi_guid = ACPI_TABLE_GUID, acpi2_guid = ACPI_20_TABLE_GUID;

	//The OS loader locates the pointer to the RSDP structure by examining 
	//the EFI Configuration Table within the EFI System Table. 
	//EFI Configuration Table entries consist of Globally Unique Identifier 
	//(GUID)/table pointer pairs. The UEFI specification defines two GUIDs for ACPI; 
	//one for ACPI 1.0 and the other for ACPI 2.0 or later specification revisions.

	for (uintn_t index = 0; index < ST->NumberOfTableEntries; index++){
		if (!memcmp(&configTable[index].VendorGuid, &acpi2_guid,16)){
			if (!memcmp("RSD PTR ", configTable[index].VendorTable, 8)){
				rsdp = (void*)configTable[index].VendorTable;
				printf("Found ACPI 2.0 rsdp at 0x%p\n",rsdp);
				break;
			}
		} else if (!memcmp(&configTable[index].VendorGuid, &acpi_guid,16)){
			if (!memcmp("RSD PTR ", configTable[index].VendorTable, 8)){
				rsdp = (void*)configTable[index].VendorTable;
				printf("Found ACPI 1.0 rsdp %p\n",rsdp);
				//found 1.0 table, save but keep looking
			}
		}
	}
	return rsdp;

}

void (*load_kernel())(bootinfo*){
	FILE* exe=fopen("kernel.elf","r");
	struct stat st;
	fstat(exe,&st);
	uint8_t* program =  malloc(st.st_size);
	fread(program,st.st_size,1,exe);
	fclose(exe);

	Elf64_Ehdr* header=(Elf64_Ehdr*)program;

	Elf64_Phdr* phdrs=(Elf64_Phdr*)(program+header->e_phoff);

	for (
		Elf64_Phdr* phdr = phdrs;
		(char*)phdr < (char*)phdrs + header->e_phnum * header->e_phentsize;
		phdr = (Elf64_Phdr*)((char*)phdr + header->e_phentsize)
	)
	{//load each program segment at the memory location indicated by its header in p_addr
		switch (phdr->p_type){
			case PT_LOAD:{
				int pages = (phdr->p_memsz + 0x1000 - 1) / 0x1000;
				Elf64_Addr paddr = phdr->p_paddr;

				ST->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, pages, &paddr);

				memcpy((void*)paddr,&program[phdr->p_offset],phdr->p_filesz);
				break;
			}
		}
	}
	void (*KernelStart)(bootinfo*) = ((__attribute__((sysv_abi)) void (*)(bootinfo*) )header->e_entry);

	return KernelStart;
}

//fix stack not being aligned to 16 bytes
__attribute__((force_align_arg_pointer))
int main(int argc, char **argv){
    efi_loaded_image_protocol_t *loaded_image;
    efi_status_t status;

    // Define the GUID variable (cannot pass macro directly)
    efi_guid_t LoadedImageProtocolGUID = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    // Retrieve the Loaded Image Protocol using HandleProtocol
    status = BS->HandleProtocol(IM, &LoadedImageProtocolGUID, (void **)&loaded_image);
    if (EFI_ERROR(status)) {
        printf("HandleProtocol failed: 0x%lx\n", status);
        return status;
    }

    // Print the actual base address of the loaded image
    printf("Image loaded at: 0x%lx\n", (uint64_t)loaded_image->ImageBase);

    volatile uint64_t *marker_ptr = (uint64_t *)0x10000;
    volatile uint64_t *image_base_ptr = (uint64_t *)0x10008;
    *image_base_ptr = (uint64_t)loaded_image->ImageBase;  // Store ImageBase
    *marker_ptr = 0xDEADBEEF;   // Set marker


    printf("Hello, world!\n");

    void* rsdp=find_rsdp();

	void(*KernelStart)(bootinfo*)=load_kernel();
	printf("kernel entry at %p\n",KernelStart);


	//do last after all other allocations
	uintn_t map_key = get_mem_map(NULL);


	printf("Exiting UEFI boot services and entering kernel\n");
	ST->BootServices->ExitBootServices(IM,map_key);

	//setup null stack frame
	asm volatile("xor %rbp, %rbp");
	KernelStart(rsdp);

	printf("kernel returned unexpectedly\n");

	return EFI_SUCCESS;
}
