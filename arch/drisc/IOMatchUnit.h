// -*- c++ -*-
#ifndef IOMATCHUNIT_H
#define IOMATCHUNIT_H

#include <sim/kernel.h>
#include <sim/inspect.h>
#include <arch/simtypes.h>
#include <map>
#include "forward.h"

namespace Simulator
{
namespace drisc
{

class IOMatchUnit : public Object, public Inspect::Interface<Inspect::Info>
{
public:
    enum AccessMode
    {
        READ = 1,
        WRITE = 2,
        READWRITE = 3
    };

protected:
    struct ComponentInterface
    {
        MemSize         size;
        AccessMode      mode;
        MMIOComponent*  component;
    };

    typedef std::map<MemAddr, ComponentInterface> RangeMap;
    RangeMap m_ranges;

    RangeMap::const_iterator FindInterface(MemAddr address, MemSize size) const;

public:
    IOMatchUnit(const std::string& name, Object& parent);

    bool IsRegisteredReadAddress(MemAddr address, MemSize size) const;
    bool IsRegisteredWriteAddress(MemAddr address, MemSize size) const;

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

    void RegisterComponent(MemAddr base, AccessMode mode, MMIOComponent& component);

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
};


class MMIOComponent : public Object
{
public:
    MMIOComponent(const std::string& name, Object& parent);

    void Connect(IOMatchUnit& mmio, IOMatchUnit::AccessMode mode);


    virtual size_t GetSize() const = 0;


    virtual Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback) = 0;
    virtual Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid) = 0;

    virtual ~MMIOComponent() {};
};

}
}


#endif
