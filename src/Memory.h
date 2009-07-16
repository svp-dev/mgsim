#ifndef MEMORY_H
#define MEMORY_H

#include "simtypes.h"
#include "ports.h"

namespace Simulator
{

// Maximum memory operation (read/write) size, in bytes. This is used to
// allocate fixed-size arrays in request buffers.
static const size_t MAX_MEMORY_OPERATION_SIZE = 64;

struct MemData
{
    MemTag  tag;
    char    data[MAX_MEMORY_OPERATION_SIZE];
    MemSize size;
};

class IMemory;

class IMemoryCallback
{
public:
    virtual bool OnMemoryReadCompleted(const MemData& data) = 0;
    virtual bool OnMemoryWriteCompleted(const MemTag& tag) = 0;
    virtual bool OnMemorySnooped(MemAddr /* addr */, const MemData& /* data */) { return true; }

    virtual ~IMemoryCallback() {}
};

class IMemory
{
public:
	static const int PERM_EXECUTE = 1;
	static const int PERM_WRITE   = 2;
	static const int PERM_READ    = 4;

    virtual void Reserve(MemAddr address, MemSize size, int perm) = 0;
    virtual void Unreserve(MemAddr address) = 0;
    virtual void RegisterListener  (PSize pid, IMemoryCallback& callback, const ArbitrationSource* sources) = 0;
    virtual void UnregisterListener(PSize pid, IMemoryCallback& callback) = 0;
    virtual bool Read (IMemoryCallback& callback, MemAddr address, MemSize size, MemTag tag) = 0;
    virtual bool Write(IMemoryCallback& callback, MemAddr address, const void* data, MemSize size, MemTag tag) = 0;
	virtual bool CheckPermissions(MemAddr address, MemSize size, int access) const = 0;

    virtual ~IMemory() {}
};

class IMemoryAdmin : public IMemory
{
public:
    virtual bool Allocate(MemSize size, int perm, MemAddr& address) = 0;
    virtual void Read (MemAddr address, void* data, MemSize size) = 0;
    virtual void Write(MemAddr address, const void* data, MemSize size) = 0;

    virtual ~IMemoryAdmin() {}
};

}
#endif

