#ifndef LOADER_H
#define LOADER_H

#include "Memory.h"

// Load the program file into the memory
Simulator::MemAddr LoadProgram(Simulator::IMemoryAdmin* memory, const std::string& path, bool quiet);

#endif

