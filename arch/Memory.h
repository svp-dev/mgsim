#ifndef MEMORY_H
#define MEMORY_H

#include "simtypes.h"
#include "symtable.h"
#include <sim/ports.h>
#include <sim/storage.h>

namespace Simulator
{

// Maximum memory operation (read/write) size, in bytes. This is used to
// allocate fixed-size arrays in request buffers.
static const size_t MAX_MEMORY_OPERATION_SIZE = 64;

struct MemData
{
    char    data[MAX_MEMORY_OPERATION_SIZE];
    bool    mask[MAX_MEMORY_OPERATION_SIZE];
};

namespace line {

    // Utility functions to merge/set lines according to mask
    template<typename T, typename M, typename S>
    void blit(T* dst, const T* src, const M* mask, S sz)
    {
        for (S i = 0; i < sz; ++i)
            if (mask[i])
                dst[i] = src[i];
    }
    
    template<typename T, typename M, typename S>
    void blitnot(T* dst, const T* src, const M* mask, S sz)
    {
        for (S i = 0; i < sz; ++i)
            if (!mask[i])
                dst[i] = src[i];
    }
    
    template<typename T, typename M, typename S>
    void setif(T* dst, const T& src, const M* mask, S sz)
    {
        for (S i = 0; i < sz; ++i)
            if (mask[i])
                dst[i] = src;
    }
    
    template<typename T, typename M, typename S>
    void setifnot(T* dst, const T& src, const M* mask, S sz)
    {
        for (S i = 0; i < sz; ++i)
            if (!mask[i])
                dst[i] = src;
    }
    
}

class IMemory;

class IMemoryCallback
{
public:
    virtual bool OnMemoryReadCompleted(MemAddr addr, const char* data) = 0;
    virtual bool OnMemoryWriteCompleted(WClientID wid) = 0;
    virtual bool OnMemoryInvalidated(MemAddr addr) = 0;
    virtual bool OnMemorySnooped(MemAddr /* addr */, const char* /*data*/, const bool* /*mask*/) { return true; }

    virtual ~IMemoryCallback() {}

    // For topology detection.
    virtual Object& GetMemoryPeer() = 0;
};

typedef size_t MCID;    ///< Memory Client ID

class IMemory
{
public:
    enum Permissions {
        PERM_EXECUTE   = 1,
	PERM_WRITE     = 2,
	PERM_READ      = 4,
        PERM_DCA_READ  = 8,
        PERM_DCA_WRITE = 16
    };

    virtual MCID RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool grouped = false) = 0;
    virtual void UnregisterClient(MCID id) = 0;
    virtual bool Read (MCID id, MemAddr address) = 0;
    virtual bool Write(MCID id, MemAddr address, const MemData& data, WClientID wid) = 0;
    
    virtual void Initialize() {}

    virtual ~IMemory() {}

    virtual void GetMemoryStatistics(uint64_t& nreads, uint64_t& nwrites, 
                                     uint64_t& nread_bytes, uint64_t& nwrite_bytes,
                                     uint64_t& nreads_ext, uint64_t& nwrites_ext) const = 0;
};

class IMemoryAdmin
{
public:
    virtual void Reserve(MemAddr address, MemSize size, ProcessID pid, int perm) = 0;
    virtual void Unreserve(MemAddr address, MemSize size) = 0;
    virtual void UnreserveAll(ProcessID pid) = 0;
    virtual bool CheckPermissions(MemAddr address, MemSize size, int access) const = 0;

    virtual void Read (MemAddr address, void* data, MemSize size) const = 0;
    virtual void Write(MemAddr address, const void* data, const bool* mask, MemSize size) = 0;

    virtual SymbolTable& GetSymbolTable() const = 0;
    virtual void SetSymbolTable(SymbolTable& symtable) = 0;

    virtual ~IMemoryAdmin() {}
};

}
#endif

