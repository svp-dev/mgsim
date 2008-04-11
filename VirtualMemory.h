#ifndef VIRTUALMEMORY_H
#define VIRTUALMEMORY_H

#include "simtypes.h"
#include <map>

namespace Simulator
{

// This class presents as large, sparse memory region as a linear memory region.
// It allocates blocks of memory as they are read or written.
class VirtualMemory
{
public:
	// We allocate per block, this is the size of each block. Must be a power of two
	static const int BLOCK_SIZE = (1 << 12);
	struct Block
	{
		char* data;
		int   permissions;
	};

	typedef std::map<size_t, Block> BlockMap;

    void read (MemAddr address, void* data, MemSize size) const;
	void write(MemAddr address, const void* data, MemSize size, int perm = 0);
	bool CheckPermissions(MemAddr address, MemSize size, int access) const;

	virtual ~VirtualMemory();

	const BlockMap& getBlockMap() const throw() { return m_blocks; }
private:
	
	BlockMap m_blocks;
};

}
#endif

