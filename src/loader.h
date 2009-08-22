#ifndef LOADER_H
#define LOADER_H

#include "Memory.h"
#include <algorithm>

// Load the program file into the memory.
// Returns loaded address and whether the file contains legacy code.
std::pair<Simulator::MemAddr, bool> LoadProgram(Simulator::IMemoryAdmin* memory, const std::string& path, bool quiet);

// Load a data file into the memory. Returns loaded address.
Simulator::MemAddr LoadDataFile(Simulator::IMemoryAdmin* memory, const std::string& path, bool quiet);

#endif

