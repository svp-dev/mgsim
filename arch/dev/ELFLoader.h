#ifndef ELFLOADER_H
#define ELFLOADER_H

#include "Memory.h"
#include "ActiveROM.h"
#include <algorithm>

// Load the program file into the memory.
// Arguments:
// - ranges: the set of loadable virtual ranges in the system memory address space
// - memory: the memory administrative interface to set permissions
// - elf_image_data: raw ELF data
// - elf_image_size: size of raw ELF data
// - quiet: set to true to enable verbose reporting of the ELF loading process
std::pair<Simulator::MemAddr, bool>
    LoadProgram(const std::string& msg_prefix,
                std::vector<Simulator::ActiveROM::LoadableRange>& ranges, 
                Simulator::IMemoryAdmin& memory,
                char *elf_image_data,
                Simulator::MemSize elf_image_size,
                bool verbose);

#endif

