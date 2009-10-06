#include "VirtualMemory.h"
#include "Memory.h"
#include "except.h"
#include <cstring>
#include <limits>
#include <iomanip>
#include <iostream>
#include <cstdlib>

using namespace std;

namespace Simulator
{

// We allocate per block, this is the size of each block. Must be a power of two
static const int BLOCK_SIZE = (1 << 12);

// Align allocations on 64 bytes
static const MemAddr ALIGNMENT = 64;

static MemAddr ALIGN_UP(const MemAddr& a)
{
    return (a + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
}

static MemAddr ALIGN_DOWN(const MemAddr& a)
{
    return a / ALIGNMENT * ALIGNMENT;
}

bool VirtualMemory::Allocate(MemSize size, int perm, MemAddr& address)
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
        const MemAddr cur_end    = ALIGN_UP(cur->first + cur->second.size);
        const MemAddr next_begin = ALIGN_DOWN(next->first);
        if (next_begin - cur_end >= size)
        {
            // Found a free range
            address = cur_end;
            Reserve(address, size, perm);
            return true;
        }
    }
    
    const MemAddr cur_end = ALIGN_UP(cur->first + cur->second.size);
    if (numeric_limits<MemAddr>::max() - cur_end >= size - 1)
    {
        // There's room after the last reserved region
        address = cur_end;
        Reserve(address, size, perm);
        return true;
    }
    
    // No free range
    return false;
}

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

void VirtualMemory::Write(MemAddr address, const void* _data, MemSize size)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

	MemAddr     base   = address & -BLOCK_SIZE;		        // Base address of block containing address
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

VirtualMemory::~VirtualMemory()
{
}

void VirtualMemory::Cmd_Info(ostream& out, const vector<string>& /* arguments */) const
{
    out << "Reserved memory ranges:" << endl
        << "------------------------" << endl;

    out << hex << setfill('0');

    MemSize total = 0;
    RangeMap::const_iterator p = m_ranges.begin();
    if (p != m_ranges.end())
    {
        // We have at least one range, walk over all ranges and
        // coalesce neighbouring ranges with similar properties.
        MemAddr begin = p->first;
        int     perm  = p->second.permissions;
        MemAddr size  = 0;

        do
        {
            size  += p->second.size;
            total += p->second.size;
            p++;
            if (p == m_ranges.end() || p->first > begin + size || p->second.permissions != perm)
            {
                // Different block, or end of blocks
                out << setw(16) << begin << " - " << setw(16) << begin + size - 1 << ": ";
                out << (perm & IMemory::PERM_READ    ? "R" : ".");
                out << (perm & IMemory::PERM_WRITE   ? "W" : ".");
                out << (perm & IMemory::PERM_EXECUTE ? "X" : ".") << endl;
                if (p != m_ranges.end())
                {
                    // Different block
                    begin = p->first;
                    perm  = p->second.permissions;
                    size  = 0;
                }
            }
        } while (p != m_ranges.end());
    }

    static const char* Mods[] = { "B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };

    // Print total memory reservation
    int mod;
    for(mod = 0; total >= 1024 && mod < 9; ++mod)
    {
        total /= 1024;
    }
    out << endl << setfill(' ') << dec;
    out << "Total reserved memory:  " << setw(4) << total << " " << Mods[mod] << endl;

    total = 0;
    for (BlockMap::const_iterator p = m_blocks.begin(); p != m_blocks.end(); ++p)
    {
        total += BLOCK_SIZE;
    }
    // Print total memory usage
    for (mod = 0; total >= 1024 && mod < 4; ++mod)
    {
        total /= 1024;
    }
    out << "Total allocated memory: " << setw(4) << total << " " << Mods[mod] << endl;
}

void VirtualMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    MemAddr addr = 0;
    MemSize size = 0;
    char* endptr = NULL;
    
    if (arguments.size() == 2)
    {
        addr = (MemAddr)strtoull( arguments[0].c_str(), &endptr, 0 );
        if (*endptr == '\0')
        {
            size = strtoul( arguments[1].c_str(), &endptr, 0 );
        }
    }

    if (arguments.size() != 2 || *endptr != '\0')
    {
        out << "Usage: read <mem> <address> <count>" << endl;
        return;
    }

    static const unsigned int BYTES_PER_LINE = 16;

    // Calculate aligned start and end addresses
    MemAddr start = addr / BYTES_PER_LINE * BYTES_PER_LINE;
    MemAddr end   = (addr + size + BYTES_PER_LINE - 1) / BYTES_PER_LINE * BYTES_PER_LINE;

    try
    {
        // Read the data
        vector<uint8_t> buf((size_t)size);
        Read(addr, &buf[0], size);

        // Print it
        for (MemAddr y = start; y < end; y += BYTES_PER_LINE)
        {
            // The address
            out << setw(8) << hex << setfill('0') << y << " | ";

            // The bytes
            for (MemAddr x = y; x < y + BYTES_PER_LINE; ++x)
            {
                if (x >= addr && x < addr + size)
                    out << setw(2) << (unsigned int)buf[(size_t)(x - addr)];
                else
                    out << "  ";
                    
                // Print some space at half the grid
                if ((x - y) == BYTES_PER_LINE / 2 - 1) out << "  ";
                out << " ";
            }
            out << "| ";

            // The bytes, as characters
            for (MemAddr x = y; x < y + BYTES_PER_LINE; ++x)
            {
                char c = ' ';
                if (x >= addr && x < addr + size) {
                    c = buf[(size_t)(x - addr)];
                    c = (isprint(c) ? c : '.');
                }
                out << c;
            }
            out << endl;
        }
    }
    catch (exception &e)
    {
        out << "An exception occured while reading the memory:" << endl;
        out << e.what() << endl;
    }
}

}

