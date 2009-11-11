#ifndef MEMDUMP_H
#define MEMDUMP_H

#include "Memory.h"

void DumpMemory(Simulator::IMemoryAdmin* memory, const std::string& path, 
		Simulator::MemAddr address, Simulator::MemSize size);

#define DEFAULT_DUMP_SIZE 0x2000000

#endif
