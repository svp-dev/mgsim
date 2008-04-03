#include "VirtualMemory.h"
#include "except.h"
using namespace std;

namespace Simulator
{

void VirtualMemory::read(MemAddr address, void* _data, MemSize size)
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	size_t base   = (size_t)address & -BLOCK_SIZE;	// Base address of block containing address
	size_t offset = (size_t)address - base;			// Offset within base block of address
	char*  data   = static_cast<char*>(_data);		// Byte-aligned pointer to destination

	// Split up the request in case it traverses a block boundary
	BlockMap::iterator pos = m_blocks.begin();
	while (size > 0)
	{
		// Find or insert the block
		pos = m_blocks.insert(pos, make_pair(base, (char*)NULL));
		if (pos->second == NULL) {
			// A new element was inserted, allocate and set memory
			pos->second = new char[BLOCK_SIZE];
		}
		
		// Number of bytes to read, initially
		size_t count = min( (size_t)size, (size_t)BLOCK_SIZE);

		// Read data
		memcpy(data, pos->second + offset, count);
		size  -= count;
		data  += count;
		base  += BLOCK_SIZE;
		offset = 0;
	}
}

void VirtualMemory::write(MemAddr address, void* _data, MemSize size)
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	size_t base   = (size_t)address & -BLOCK_SIZE;	// Base address of block containing address
	size_t offset = (size_t)address - base;			// Offset within base block of address
	char*  data   = static_cast<char*>(_data);		// Byte-aligned pointer to destination

	// Split up the request in case it traverses a block boundary
	BlockMap::iterator pos = m_blocks.begin();
	while (size > 0)
	{
		// Find or insert the block
		pos = m_blocks.insert(pos, make_pair(base, (char*)NULL));
		if (pos->second == NULL) {
			// A new element was inserted, allocate and set memory
			pos->second = new char[BLOCK_SIZE];
		}
		
		// Number of bytes to read, initially
		size_t count = min( (size_t)size, (size_t)BLOCK_SIZE);

		// Read data
		memcpy(pos->second + offset, data, count);
		size  -= count;
		data  += count;
		base  += BLOCK_SIZE;
		offset = 0;
	}

}

VirtualMemory::~VirtualMemory()
{
	// Clean up all allocated memory
	for (BlockMap::const_iterator p = m_blocks.begin(); p != m_blocks.end(); p++)
	{
		delete[] p->second;
	}
}

}