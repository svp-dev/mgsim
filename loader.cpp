#include "loader.h"
#include "elf.h"
#include "except.h"
#include <fstream>
#include <iostream>
using namespace Simulator;
using namespace std;

static const int PAGE_SIZE = 4096;

// Throws an exception if the expression is false
static void Verify(bool expr, const char* error = "invalid ELF file")
{
	if (!expr) {
		throw Exception(error);
	}
}

// Load the program image into the memory
static MemAddr LoadProgram(IMemoryAdmin* memory, void* _data, MemSize size, bool quiet)
{
	char*      data =  static_cast<char*>(_data);
	Elf64_Ehdr ehdr = *static_cast<Elf64_Ehdr*>(_data);
	
	// Unmarshall header
	ehdr.e_type      = letohs (ehdr.e_type);
	ehdr.e_machine   = letohs (ehdr.e_machine);
	ehdr.e_version   = letohl (ehdr.e_version);
	ehdr.e_entry     = letohll(ehdr.e_entry);
	ehdr.e_phoff     = letohll(ehdr.e_phoff);
	ehdr.e_shoff     = letohll(ehdr.e_shoff);
	ehdr.e_flags     = letohl (ehdr.e_flags);
	ehdr.e_ehsize    = letohs (ehdr.e_ehsize);
	ehdr.e_phentsize = letohs (ehdr.e_phentsize);
	ehdr.e_phnum     = letohs (ehdr.e_phnum);
	ehdr.e_shentsize = letohs (ehdr.e_shentsize);
	ehdr.e_shnum     = letohs (ehdr.e_shnum);
	ehdr.e_shstrndx  = letohs (ehdr.e_shstrndx);

	// Check file signature
	if (size < sizeof(Elf64_Ehdr) ||
		ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
		ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3)
	{
		// Not an ELF file, load as flat binary, starts at address 0
		if (!quiet)
		{
    		cout << "Loaded flat binary to address 0" << endl;
    	}
		memory->write(0, data, size, IMemory::PERM_READ | IMemory::PERM_WRITE | IMemory::PERM_EXECUTE);
		return 0;
	}

	// Check that this file is for our 'architecture'
	Verify(ehdr.e_ident[EI_CLASS]   == ELFCLASS64,  "file is not 64-bit");
	Verify(ehdr.e_ident[EI_DATA]    == ELFDATA2LSB, "file is not little-endian");
	Verify(ehdr.e_ident[EI_VERSION] == EV_CURRENT,  "ELF version mismatch");
	Verify(ehdr.e_type              == ET_EXEC,     "file is not an executable file");
	Verify(ehdr.e_machine           == EM_ALPHA,    "target architecture is not Alpha");
	Verify(ehdr.e_phoff != 0 && ehdr.e_phnum != 0, "file has no program header");
	Verify(ehdr.e_phentsize == sizeof(Elf64_Phdr),  "file has an invalid program header");
	Verify(ehdr.e_phoff + ehdr.e_phnum * ehdr.e_phentsize <= size, "file has an invalid program header");

	Elf64_Phdr* phdr = static_cast<Elf64_Phdr*>(static_cast<void*>(data + ehdr.e_phoff));

	// Determine base address and check for loadable segments
	bool       hasLoadable = false;
	Elf64_Addr base = 0;
	for (Elf64_Half i = 0; i < ehdr.e_phnum; i++)
	{
	    phdr[i].p_type   = letohl (phdr[i].p_type);
	    phdr[i].p_flags  = letohl (phdr[i].p_flags);
	    phdr[i].p_offset = letohll(phdr[i].p_offset);
	    phdr[i].p_vaddr  = letohll(phdr[i].p_vaddr);
	    phdr[i].p_paddr  = letohll(phdr[i].p_paddr);
	    phdr[i].p_filesz = letohll(phdr[i].p_filesz);
	    phdr[i].p_memsz  = letohll(phdr[i].p_memsz);
	    phdr[i].p_align  = letohll(phdr[i].p_align);
	    
		if (phdr[i].p_type == PT_LOAD)
		{
			if (!hasLoadable || phdr[i].p_vaddr < base) {
				base = phdr[i].p_vaddr;
			}
			hasLoadable = true;
		}
	}
	Verify(hasLoadable, "file has no loadable segments");
	base = base & -PAGE_SIZE;

	// Then copy the LOAD segments into their right locations
	for (Elf64_Half i = 0; i < ehdr.e_phnum; i++)
	{
		if (phdr[i].p_type == PT_LOAD)
		{
  			Verify(phdr[i].p_memsz >= phdr[i].p_filesz, "file has an invalid segment");
  			
			int perm = 0;
			if (phdr[i].p_flags & PF_R) perm |= IMemory::PERM_READ;
			if (phdr[i].p_flags & PF_W) perm |= IMemory::PERM_WRITE;
			if (phdr[i].p_flags & PF_X) perm |= IMemory::PERM_EXECUTE;

			if (phdr[i].p_filesz > 0)
			{
				Verify(phdr[i].p_offset + phdr[i].p_filesz <= size, "file has an invalid segment");

				memory->write(phdr[i].p_vaddr, data + phdr[i].p_offset, phdr[i].p_filesz, perm);
			}

			// Clear the difference between filesz and memsz
			static const char zero[256] = {0};
			Elf64_Xword size = phdr[i].p_memsz - phdr[i].p_filesz;
			while (size > 0)
			{
				Elf64_Xword num  = min(size, 256ULL);
				Elf64_Addr addr = phdr[i].p_vaddr + phdr[i].p_filesz;
				memory->write(addr, zero, num, perm);
				size -= num;
				addr += num;
			}
		}
	}
	
	if (!quiet)
	{
    	cout << "Loaded ELF binary at address 0x" << hex << base << endl;
    	cout << "Entry point: 0x" << hex << ehdr.e_entry << endl;
    }
	return ehdr.e_entry;
}

// Load the program file into the memory
MemAddr LoadProgram(IMemoryAdmin* memory, const string& path, bool quiet)
{
    ifstream input(path.c_str(), ios::binary);
    if (!input.is_open() || !input.good())
    {
        throw Exception("Unable to load program: " + path);
    }

	// Read the entire file
    input.seekg(0, ios::end);
    streampos size = input.tellg();
    char* data = new char[size];
	try
	{
		input.seekg(0, ios::beg);
		input.read(data, size);
		MemAddr entry = LoadProgram(memory, data, size, quiet);
		delete[] data;
		return entry;
	}
	catch (Exception& e)
	{
		delete[] data;
		throw Exception(string("Unable to load program: ") + e.getMessage());
	}
}
