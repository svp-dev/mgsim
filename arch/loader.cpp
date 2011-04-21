#include "loader.h"
#include "elf.h"
#include "sim/except.h"
#include <fstream>
#include <iostream>
#include <vector>

using namespace std;
using namespace Simulator;

static const int PAGE_SIZE = 4096;

// Throws an exception if the expression is false
static void Verify(bool expr, const char* error = "invalid ELF file")
{
    if (!expr) {
        throw runtime_error(error);
    }
}

// Load a data file into the memory
std::pair<MemAddr, size_t> LoadDataFile(IMemoryAdmin* memory, const string& path, bool quiet)
{
    ifstream input(path.c_str(), ios::binary);
    if (!input.is_open() || !input.good())
    {
        throw runtime_error("Unable to open file \"" + path + "\"");
    }

    input.seekg(0, ios::end);
    streamsize fsize = (streamsize)input.tellg();
    streamsize dsize = (fsize + 8) & -8; // Pad to 8 bytes
    vector<char> data(dsize);

    input.seekg(0, ios::beg);
    input.read(&data[0], fsize);

    MemAddr base;
    if (!memory->Allocate(dsize, IMemory::PERM_READ | IMemory::PERM_WRITE, base))
    {
        throw runtime_error("Unable to allocate memory to load data file");
    }
    memory->Write(base, &data[0], dsize);

    if (!quiet)
    {
        cout << "Loaded " << dec << dsize << " bytes of data at address 0x" << hex << base
             << " from: \"" << path << "\"" << endl;
    }
    return make_pair(base, dsize);
}

static bool IsReserved(MemAddr address, MemSize size)
{
    // The following memory ranges are reserved:

    // The lower 256 bytes of memory, which contains profiling information
    if (address < 256) return true;
    
    // The upper half of memory, which contains the TLS
    if (address + size > ((MemAddr)1 << (sizeof(MemAddr) * 8 - 1)) ) return true;
    
    return false;
}

// Load the program image into the memory
static std::pair<MemAddr, bool> LoadProgram(IMemoryAdmin* memory, void* _data, MemSize size, bool quiet)
{
    char*    data =  static_cast<char*>(_data);
    Elf_Ehdr ehdr = *static_cast<Elf_Ehdr*>(_data);
    
    // Unmarshall header
    ehdr.e_type      = elftohh(ehdr.e_type);
    ehdr.e_machine   = elftohh(ehdr.e_machine);
    ehdr.e_version   = elftohw(ehdr.e_version);
    ehdr.e_entry     = elftoha(ehdr.e_entry);
    ehdr.e_phoff     = elftoho(ehdr.e_phoff);
    ehdr.e_shoff     = elftoho(ehdr.e_shoff);
    ehdr.e_flags     = elftohw(ehdr.e_flags);
    ehdr.e_ehsize    = elftohh(ehdr.e_ehsize);
    ehdr.e_phentsize = elftohh(ehdr.e_phentsize);
    ehdr.e_phnum     = elftohh(ehdr.e_phnum);
    ehdr.e_shentsize = elftohh(ehdr.e_shentsize);
    ehdr.e_shnum     = elftohh(ehdr.e_shnum);
    ehdr.e_shstrndx  = elftohh(ehdr.e_shstrndx);

    // Check file signature
    Verify(size >= sizeof(Elf_Ehdr) &&
        ehdr.e_ident[EI_MAG0] == ELFMAG0 && ehdr.e_ident[EI_MAG1] == ELFMAG1 &&
        ehdr.e_ident[EI_MAG2] == ELFMAG2 && ehdr.e_ident[EI_MAG3] == ELFMAG3,
        "invalid ELF file signature");

    // Check that this file is for our 'architecture'
    Verify(ehdr.e_ident[EI_VERSION] == EV_CURRENT,  "ELF version mismatch");
    Verify(ehdr.e_ident[EI_CLASS]   == ELFCLASS,    "file is not of proper bitsize");
    Verify(ehdr.e_ident[EI_DATA]    == ELFDATA,     "file is not of proper endianness");
    Verify(ehdr.e_machine == MACHINE_NORMAL ||
           ehdr.e_machine == MACHINE_LEGACY,       "target architecture is not supported");
    Verify(ehdr.e_type              == ET_EXEC,    "file is not an executable file");
    Verify(ehdr.e_phoff != 0 && ehdr.e_phnum != 0, "file has no program header");
    Verify(ehdr.e_phentsize == sizeof(Elf_Phdr),   "file has an invalid program header");
    Verify(ehdr.e_phoff + ehdr.e_phnum * ehdr.e_phentsize <= size, "file has an invalid program header");

    Elf_Phdr* phdr = static_cast<Elf_Phdr*>(static_cast<void*>(data + ehdr.e_phoff));

    // Determine base address and check for loadable segments
    bool     hasLoadable = false;
    Elf_Addr base = 0;
    for (Elf_Half i = 0; i < ehdr.e_phnum; ++i)
    {
        phdr[i].p_type   = elftohw (phdr[i].p_type);
        phdr[i].p_flags  = elftohw (phdr[i].p_flags);
        phdr[i].p_offset = elftoho (phdr[i].p_offset);
        phdr[i].p_vaddr  = elftoha (phdr[i].p_vaddr);
        phdr[i].p_paddr  = elftoha (phdr[i].p_paddr);
        phdr[i].p_filesz = elftohxw(phdr[i].p_filesz);
        phdr[i].p_memsz  = elftohxw(phdr[i].p_memsz);
        phdr[i].p_align  = elftohxw(phdr[i].p_align);
        
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
    for (Elf_Half i = 0; i < ehdr.e_phnum; ++i)
    {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz > 0)
        {
            Verify(phdr[i].p_memsz >= phdr[i].p_filesz, "file has an invalid segment");
            
            int perm = 0;
            if (phdr[i].p_flags & PF_R) perm |= IMemory::PERM_READ;
            if (phdr[i].p_flags & PF_W) perm |= IMemory::PERM_WRITE;
            if (phdr[i].p_flags & PF_X) perm |= IMemory::PERM_EXECUTE;
            
            // Check if the range is special, "reserved", memory
            Verify(!IsReserved(phdr[i].p_vaddr, phdr[i].p_memsz), "section located at reserved memory");
        
            // Reserve the range
            memory->Reserve(phdr[i].p_vaddr, phdr[i].p_memsz, perm);
            
            if (phdr[i].p_filesz > 0)
            {
                Verify(phdr[i].p_offset + phdr[i].p_filesz <= size, "file has an invalid segment");

                memory->Write(phdr[i].p_vaddr, data + phdr[i].p_offset, phdr[i].p_filesz);

            }
        }
    }
    
    if (!quiet)
    {
        const char* type = (ehdr.e_machine == MACHINE_LEGACY)
            ? "legacy"
            : "microthreaded";
            
        cout << "Loaded " << type << " ELF binary at address 0x" << hex << base << endl;
        cout << "Entry point: 0x" << hex << ehdr.e_entry << endl;
    }
    return make_pair(ehdr.e_entry, ehdr.e_machine == MACHINE_LEGACY);
}

// Load the program file into the memory
std::pair<MemAddr, bool> LoadProgram(IMemoryAdmin* memory, const string& path, bool quiet)
{
    ifstream input(path.c_str(), ios::binary);
    if (!input.is_open() || !input.good())
    {
        throw runtime_error("Unable to open file \"" + path + "\"");
    }

    // Read the entire file
    input.seekg(0, ios::end);
    streampos size = input.tellg();
    vector<char> data( (size_t)size );
    try
    {
        input.seekg(0, ios::beg);
        input.read(&data[0], data.size());
        return LoadProgram(memory, &data[0], data.size(), quiet);
    }
    catch (exception& e)
    {
        throw runtime_error("Unable to load program \"" + path + "\":\n" + e.what());
    }
}
