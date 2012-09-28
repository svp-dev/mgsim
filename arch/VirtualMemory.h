#ifndef VIRTUALMEMORY_H
#define VIRTUALMEMORY_H

#include <arch/simtypes.h>
#include <sim/inspect.h>

#include <map>
#include <vector>

namespace Simulator
{

// This class presents as large, sparse memory region as a linear memory region.
// It allocates blocks of memory as they are read or written.
class VirtualMemory : public Inspect::Interface<Inspect::Info|Inspect::Read>
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
        MemSize   size;
        ProcessID owner;
        int       permissions;
    };
    
    typedef std::map<MemAddr, Block> BlockMap;
    typedef std::map<MemAddr, Range> RangeMap;
    
    void Reserve(MemAddr address, MemSize size, ProcessID pid, int perm);
    void Unreserve(MemAddr address, MemSize size);
    void UnreserveAll(ProcessID pid);
    
    void Read (MemAddr address, void* data, MemSize size) const;

    // Mask indicates which bytes of the data to actually write. If mask is set
    // to NULL, then write all bytes.
    void Write(MemAddr address, const void* data, const bool* mask, MemSize size);
    
    bool CheckPermissions(MemAddr address, MemSize size, int access) const;

    VirtualMemory();    
    virtual ~VirtualMemory();
    
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
private:
    RangeMap::const_iterator GetReservationRange(MemAddr address, MemSize size) const;
    void ReportOverlap(MemAddr address, MemSize size) const;

    BlockMap m_blocks;
    RangeMap m_ranges;
    size_t   m_totalreserved;
    size_t   m_totalallocated;
    size_t   m_nRanges;
};

}
#endif

