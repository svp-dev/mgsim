#ifndef _MEMORY_DATA_CONTAINER_H
#define _MEMORY_DATA_CONTAINER_H
#include "smdatatype.h"
#include "../memorys/dcswitch.h"

class MemoryDataContainer
{
private:

public:
    static MemoryDataContainer * s_pDataContainer;

    MemoryDataContainer();

	virtual ~MemoryDataContainer(){};

    // load memory image file from certain address
    virtual void LoadMemoryImage(char* imgfile, uint64_t addr) = 0;

    // load memory image file. 
    virtual void LoadMemoryImage(char* imgfile) = 0;

    // update the data from a certain address
    // return : whether the operation is successful
    // ??? permission to be defined
    virtual bool Update(uint64_t startingAddress, uint64_t size, char* data, unsigned char permission = 0) = 0;

    virtual bool UpdateAllocate(uint64_t& startingAddress, uint64_t size, unsigned char permission = 0) = 0;

    virtual bool UpdateReserve(uint64_t startingAddress, uint64_t size, unsigned char permission) = 0;

    virtual bool UpdateUnreserve(uint64_t startingAddress) = 0;

    // fetch data from the certain address
    // return : whether the operation is successful
    virtual bool Fetch(uint64_t startingAddress, uint64_t size, char* data) = 0;

#ifdef MEM_DATA_VERIFY
    // verify the validness of a piece of data
    virtual bool Verify(uint64 startingAddress, uint64_t size, char* data) = 0;

    // verify update, update for every write
    virtual bool VerifyUpdate(uint64_t startingAddress, uint64_t size, char* data) = 0;
#endif
    // print the data in memory
    // return : whether the operation is successful
    // method : 0 - normal, standalone 
    // method : 1 - one line mode
    virtual void PrintData(unsigned int method=0) = 0;
};

#endif

