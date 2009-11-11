#ifndef _V_MEMORY_DATA_CONTAINER_H
#define _V_MEMORY_DATA_CONTAINER_H

#include "predef.h"
#include "../simlink/memorydatacontainer.h"

#include <stdint.h>
#include <limits>

//#define MEM_DATA_CONTAINER_GRANULARITY_BIT  21

namespace MemSim{
// *************************************
// Implementation mainly cped from MGSIM
// *************************************

//{ memory simulator namespace
//////////////////////////////
class VMemoryDataContainer : public MemoryDataContainer, public SimObj
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
        uint64_t size;
        int     permissions;
    };

	typedef std::map<uint64_t, Block> BlockMap;
	typedef std::map<uint64_t, Range> RangeMap;
	
	bool Allocate(uint64_t size, int perm, uint64_t& address);
	
	void Reserve(uint64_t address, uint64_t size, int perm);
	void Unreserve(uint64_t address);

    void Read (uint64_t address, void* data, uint64_t size) const;
	void Write(uint64_t address, const void* data, uint64_t size);
	
	bool CheckPermissions(uint64_t address, uint64_t size, int access) const;

	virtual ~VMemoryDataContainer();

	const RangeMap& GetRangeMap() const { return m_ranges; }
	const BlockMap& GetBlockMap() const { return m_blocks; }
private:
	RangeMap::const_iterator GetReservationRange(uint64_t address, uint64_t size) const;
	
	BlockMap m_blocks;
	RangeMap m_ranges;

public:
    // load memory image file from certain address
    virtual void LoadMemoryImage(char* imgfile, uint64_t addr){};

    // load memory image file. 
    virtual void LoadMemoryImage(char* imgfile){};

    // update the data from a certain address
    // return : whether the operation is successful
    // ??? permission to be defined
    virtual bool Update(uint64_t startingAddress, uint64_t size, char* data, unsigned char permission = 0)
    {
        Write(startingAddress, (const void*)data, size);

        return true;
    }

    virtual bool UpdateAllocate(uint64_t& startingAddress, uint64_t size, unsigned char permission = 0)
    {
        return Allocate(size, permission, startingAddress);
    }

    virtual bool UpdateReserve(uint64_t startingAddress, uint64_t size, unsigned char permission)
    {
        Reserve(startingAddress, size, permission);

        return true;
    }

    virtual bool UpdateUnreserve(uint64_t startingAddress)
    {
        Unreserve(startingAddress);

        return true;
    }

    // fetch data from the certain address
    // return : whether the operation is successful
    virtual bool Fetch(uint64_t startingAddress, uint64_t size, char* data)
    {
        Read(startingAddress, (void*)data, size);

        return true;
    }

#ifdef MEM_DATA_VERIFY
    // verify the validness of a piece of data
    virtual bool Verify(uint64_t startingAddress, uint64_t size, char* data){return true;};

    // verify update, update for every write
    virtual bool VerifyUpdate(uint64_t startingAddress, uint64_t size, char* data){return true;};
#endif
    // print the data in memory
    // return : whether the operation is successful
    // method : 0 - normal, standalone 
    // method : 1 - one line mode
    virtual void PrintData(unsigned int method=0){};

};


//////////////////////////////
//} memory simulator namespace
}

#endif
