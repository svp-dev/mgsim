#ifndef MEMORY_H
#define MEMORY_H

#include "simtypes.h"

namespace Simulator
{

struct MemData
{
    MemTag  tag;
    void*   data;
    MemSize size;
};

class IMemory;

class IMemoryCallback
{
public:
    virtual bool onMemoryReadCompleted(const MemData& data) = 0;
    virtual bool onMemoryWriteCompleted(const MemTag& tag) = 0;
    virtual bool onMemorySnooped(MemAddr addr, const MemData& data) { return true; }

    virtual ~IMemoryCallback() {}
};

class IMemory
{
public:
    virtual void registerListener  (IMemoryCallback& callback) = 0;
    virtual void unregisterListener(IMemoryCallback& callback) = 0;
    virtual Result read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag) = 0;
    virtual Result write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag) = 0;

    virtual ~IMemory() {}
};

class IMemoryAdmin
{
public:
    virtual void read (MemAddr address, void* data, MemSize size) = 0;
    virtual void write(MemAddr address, void* data, MemSize size) = 0;

    virtual ~IMemoryAdmin() {}
};

}
#endif

