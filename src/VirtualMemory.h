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
        char data[BLOCK_SIZE];
    };

    struct Range
    {
        MemSize size;
        int     permissions;
    };

	typedef std::map<MemAddr, Block> BlockMap;
	typedef std::map<MemAddr, Range> RangeMap;
	
	void Reserve(MemAddr address, MemSize size, int perm);
	void Unreserve(MemAddr address);

    void Read (MemAddr address, void* data, MemSize size) const;
	void Write(MemAddr address, const void* data, MemSize size);
	
	bool CheckPermissions(MemAddr address, MemSize size, int access) const;

	virtual ~VirtualMemory();

	const RangeMap& GetRangeMap() const { return m_ranges; }
	const BlockMap& GetBlockMap() const { return m_blocks; }
private:
	RangeMap::const_iterator GetReservationRange(MemAddr address, MemSize size) const;
	
	BlockMap m_blocks;
	RangeMap m_ranges;
};

}
#endif

