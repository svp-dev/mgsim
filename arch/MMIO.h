#ifndef MMIO_H
#define MMIO_H

#include "simtypes.h"
#include "sim/kernel.h"
#include <map>
#include <string>
#include <iostream>
#include <vector>

class Config;

namespace Simulator
{

class MMIOComponent;
class Processor;

class MMIOInterface : public Object
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
    MMIOInterface(const std::string& name, Processor& parent, Clock& clock, const Config& config);

    Processor& GetProcessor() const;

    bool IsRegisteredReadAddress(MemAddr address, MemSize size) const;
    bool IsRegisteredWriteAddress(MemAddr address, MemSize size) const;

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

    void RegisterComponent(MemAddr base, MemSize size, AccessMode mode, MMIOComponent& component);    

    // Debuggin
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
};


class MMIOComponent : public Object
{
public:
    MMIOComponent(const std::string& name, MMIOInterface& parent, Clock& clock);

    MMIOInterface& GetInterface() { return *static_cast<MMIOInterface*>(GetParent()); }

    virtual size_t GetSize() const = 0;


    virtual Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid) = 0;
    virtual Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid) = 0;

    virtual ~MMIOComponent() {};
};



}


#endif
