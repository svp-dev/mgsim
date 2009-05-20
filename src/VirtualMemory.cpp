#include "VirtualMemory.h"
#include "except.h"
#include <cstring>
using namespace std;

namespace Simulator
{

// We allocate per block, this is the size of each block. Must be a power of two
static const int BLOCK_SIZE = (1 << 12);

void VirtualMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    if (size != 0)
    {
        // Check that there is no overlap
        RangeMap::iterator p = m_ranges.lower_bound(address);
        if (p != m_ranges.end())
        {
            if (p->first == address || (address < p->first && address + size > p->first))
            {
                // The range overlaps with an existing range after it
                throw InvalidArgumentException("Overlap in memory reservation");
            }
        
            if (p != m_ranges.begin())
            {
                RangeMap::iterator q = p; --q;
                if (q->first < address && q->first > address - q->second.size)
                {
                    // The range overlaps with an existing range before it
                    throw InvalidArgumentException("Overlap in memory reservation");
                }
            }
        }

        Range range;
        range.size        = size;
        range.permissions = perm;
        m_ranges.insert(p, make_pair(address, range));
    }
}

VirtualMemory::RangeMap::const_iterator VirtualMemory::GetReservationRange(MemAddr address, MemSize size) const
{
    RangeMap::const_iterator p = m_ranges.lower_bound(address);
    if (p != m_ranges.begin() && (p == m_ranges.end() || p->first > address))
    {
        --p;
    }
    return (p != m_ranges.end() &&
            address >= p->first && p->second.size >= size && 
            address <= p->first + (p->second.size - size)) ? p : m_ranges.end();
}

void VirtualMemory::Unreserve(MemAddr address)
{
    RangeMap::iterator p = m_ranges.find(address);
    if (p == m_ranges.end())
    {
        throw InvalidArgumentException("Attempting to unreserve non-reserved memory");
    }
    m_ranges.erase(p);
}

bool VirtualMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    RangeMap::const_iterator p = GetReservationRange(address, size);
    return (p != m_ranges.end() && (p->second.permissions & access) == access);
}

void VirtualMemory::Read(MemAddr address, void* _data, MemSize size) const
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

	MemAddr base   = address & -BLOCK_SIZE;	    // Base address of block containing address
	size_t  offset = address - base;		    // Offset within base block of address
	char*   data   = static_cast<char*>(_data);	// Byte-aligned pointer to destination

	for (BlockMap::const_iterator pos = m_blocks.lower_bound(base); size > 0;)
	{
		if (pos == m_blocks.end())
		{
			// Rest of address range does not exist, fill with zero
			fill(data, data + size, 0);
			break;
		}

		// Number of bytes to read, initially
		size_t count = min( (size_t)size, (size_t)BLOCK_SIZE);

		if (pos->first > base) {
			// This part of the request does not exist, fill with zero
			fill(data, data + count, 0);
		} else {
			// Read data
			memcpy(data, pos->second.data + offset, count);
			++pos;
		}
		size  -= count;
		data  += count;
		base  += BLOCK_SIZE;
		offset = 0;
	}
}

void VirtualMemory::Write(MemAddr address, const void* _data, MemSize size)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

	MemAddr     base   = address & -BLOCK_SIZE;		        // Base address of block containing address
	size_t      offset = address - base;			        // Offset within base block of address
	const char* data   = static_cast<const char*>(_data);	// Byte-aligned pointer to destination

	while (size > 0)
	{
		// Find or insert the block
		pair<BlockMap::iterator, bool> ins = m_blocks.insert(make_pair(base, Block()));
    	BlockMap::iterator pos = ins.first;
		if (ins.second) {
			// A new element was inserted, allocate and clear memory
			memset(pos->second.data, 0, BLOCK_SIZE);
		}
		
		// Number of bytes to write, initially
		size_t count = min( (size_t)size, (size_t)BLOCK_SIZE - offset);

		// Write data
		memcpy(pos->second.data + offset, data, count);
		size  -= count;
		data  += count;
		base  += BLOCK_SIZE;
		offset = 0;
	}
}

VirtualMemory::~VirtualMemory()
{
}

}

