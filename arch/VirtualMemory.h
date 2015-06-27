// -*- c++ -*-
#ifndef VIRTUALMEMORY_H
#define VIRTUALMEMORY_H

#include <arch/simtypes.h>
#include <arch/symtable.h>
#include <sim/inspect.h>
#include <sim/kernel.h>
#include <sim/sampling.h>
#include <arch/Memory.h>

#include <map>
#include <vector>

namespace Simulator
{

// This class presents as large, sparse memory region as a linear memory region.
// It allocates blocks of memory as they are read or written.
class VirtualMemory : public Object,
                      public IMemoryAdmin
{
public:
    // We allocate per block, this is the size of each block. Must be a power of two
    static const int BLOCK_SIZE = (1 << 12);

    struct Block
    {
        char data[BLOCK_SIZE];
        SERIALIZE(a) { a & Serialization::binary(data, BLOCK_SIZE); }
    };

    struct Range
    {
        MemSize   size;
        ProcessID owner;
        int       permissions;
        SERIALIZE(a) { a & size & owner & permissions; }
    };

    typedef std::map<MemAddr, Block> BlockMap;
    typedef std::map<MemAddr, Range> RangeMap;

    void Reserve(MemAddr address, MemSize size, ProcessID pid, int perm) override;
    void Unreserve(MemAddr address, MemSize size) override;
    void UnreserveAll(ProcessID pid) override;

    void Read (MemAddr address, void* data, MemSize size) const override;

    // Mask indicates which bytes of the data to actually write. If mask is set
    // to NULL, then write all bytes.
    void Write(MemAddr address, const void* data, const bool* mask, MemSize size) override;

    bool CheckPermissions(MemAddr address, MemSize size, int access) const override;

    VirtualMemory(const std::string& name, Object& parent);
    VirtualMemory(const VirtualMemory&) = delete;
    VirtualMemory& operator=(const VirtualMemory&) = delete;
    virtual ~VirtualMemory();

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void SetSymbolTable(SymbolTable& symtable) override;
    SymbolTable& GetSymbolTable() const override;

private:
    RangeMap::const_iterator GetReservationRange(MemAddr address, MemSize size) const;
    void ReportOverlap(MemAddr address, MemSize size) const;

    DefineStateVariable(BlockMap, blocks);
    DefineStateVariable(RangeMap, ranges);

    DefineSampleVariable(size_t, total_reserved);
    DefineSampleVariable(size_t, total_allocated);
    DefineSampleVariable(size_t, number_of_ranges);
    SymbolTable *m_symtable;
};

}
#endif
