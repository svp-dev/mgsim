#ifndef LOADER_H
#define LOADER_H

#include "Memory.h"
#include <algorithm>

// Load the program file into the memory;
// return entry pointer and end address.
std::pair<Simulator::MemAddr,Simulator::MemAddr>
 LoadProgram(Simulator::IMemoryAdmin* memory, const std::string& path, bool quiet);

// Load a data file into the memory;
// load address past the data.
Simulator::MemAddr LoadDataFile(Simulator::IMemoryAdmin* memory, const std::string& path,
				Simulator::MemAddr base, bool quiet);

#endif

