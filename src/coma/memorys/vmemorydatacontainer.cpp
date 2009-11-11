#include "vmemorydatacontainer.h"
#include <stdint.h>
using namespace MemSim;

namespace MemSim{

// *************************************
// Implementation mainly cped from MGSIM
// *************************************
// We allocate per block, this is the size of each block. Must be a power of two

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#ifndef UINT64_MAX
static const uint64_t UINT64_MAX = 0xFFFFFFFFFFFFFFFFULL;
#endif
#define MEMSIZE_MAX UINT64_MAX

// Align allocations on 64 bytes
static const uint64_t ALIGNMENT = 64;

static uint64_t ALIGN_UP(const uint64_t& a)
{
    return (a + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
}

static uint64_t ALIGN_DOWN(const uint64_t& a)
{
    return a / ALIGNMENT * ALIGNMENT;
}

bool VMemoryDataContainer::Allocate(uint64_t size, int perm, uint64_t& address)
{
    if (size == 0)
    {
        // No size, nothing to allocate
        return false;
    }
    
    // Find a free spot in the reservation table
    RangeMap::const_iterator cur = m_ranges.begin();
    if (cur == m_ranges.end())
    {
        // There's nothing reserved yet, just grab the lowest address
        address = 0;
        return true;
    }
    
    RangeMap::const_iterator next = cur;
    for (++next; next != m_ranges.end(); ++next, ++cur)
    {
        const uint64_t cur_end    = ALIGN_UP(cur->first + cur->second.size);
        const uint64_t next_begin = ALIGN_DOWN(next->first);
        if (next_begin - cur_end >= size)
        {
            // Found a free range
            address = cur_end;
            Reserve(address, size, perm);
            return true;
        }
    }
    
    const uint64_t cur_end = ALIGN_UP(cur->first + cur->second.size);
    if (numeric_limits<uint64_t>::max() - cur_end >= size - 1)
    {
        // There's room after the last reserved region
        address = cur_end;
        Reserve(address, size, perm);
        return true;
    }
    
    // No free range
    return false;
}

void VMemoryDataContainer::Reserve(uint64_t address, uint64_t size, int perm)
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
                //throw InvalidArgumentException("Overlap in memory reservation");
                assert(false);
            }
        
            if (p != m_ranges.begin())
            {
                RangeMap::iterator q = p; --q;
                if (q->first < address && q->first > address - q->second.size)
                {
                    // The range overlaps with an existing range before it
                    //throw InvalidArgumentException("Overlap in memory reservation");
                    assert(false);
                }
            }
        }

        Range range;
        range.size        = size;
        range.permissions = perm;
        m_ranges.insert(p, make_pair(address, range));
    }
}

VMemoryDataContainer::RangeMap::const_iterator VMemoryDataContainer::GetReservationRange(uint64_t address, uint64_t size) const
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

void VMemoryDataContainer::Unreserve(uint64_t address)
{
    RangeMap::iterator p = m_ranges.find(address);
    if (p == m_ranges.end())
    {
        cout << "unres " << hex << address << endl;
        //throw InvalidArgumentException("Attempting to unreserve non-reserved memory");
        assert(false);
    }
    m_ranges.erase(p);
}

bool VMemoryDataContainer::CheckPermissions(uint64_t address, uint64_t size, int access) const
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        //throw InvalidArgumentException("Size argument too big");
        assert(false);
    }
#endif

    RangeMap::const_iterator p = GetReservationRange(address, size);
    return (p != m_ranges.end() && (p->second.permissions & access) == access);
}

void VMemoryDataContainer::Read(uint64_t address, void* _data, uint64_t size) const
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        //throw InvalidArgumentException("Size argument too big");
        assert(false);
    }
#endif

	uint64_t base   = address & -BLOCK_SIZE;	    // Base address of block containing address
	size_t  offset = (size_t)(address - base);	// Offset within base block of address
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

void VMemoryDataContainer::Write(uint64_t address, const void* _data, uint64_t size)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        //throw InvalidArgumentException("Size argument too big");
        assert(false);
    }
#endif

	uint64_t     base   = address & -BLOCK_SIZE;		        // Base address of block containing address
	size_t      offset = (size_t)(address - base);          // Offset within base block of address
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

VMemoryDataContainer::~VMemoryDataContainer()
{
}


}

