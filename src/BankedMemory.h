#ifndef BANKEDMEMORY_H
#define BANKEDMEMORY_H

#include "Memory.h"
#include "buffer.h"
#include "VirtualMemory.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class ArbitratedWriteFunction;

class BankedMemory : public IComponent, public IMemoryAdmin, public VirtualMemory
{
public:
    BankedMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config);
    
    // Component
    Result OnCycleWritePhase(unsigned int stateIndex);

    // IMemory
    void   Reserve(MemAddr address, MemSize size, int perm);
    void   Unreserve(MemAddr address);
    void   RegisterListener  (PSize pid, IMemoryCallback& callback);
    void   UnregisterListener(PSize pid, IMemoryCallback& callback);
    Result Read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
    Result Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
	bool   CheckPermissions(MemAddr address, MemSize size, int access) const;

    // IMemoryAdmin
    bool Allocate(MemSize size, int perm, MemAddr& address);
    void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
private:
    struct Request
    {
        IMemoryCallback* callback;
        bool             write;
        MemAddr          address;
        MemData          data;
    };
    
    typedef std::multimap<CycleNo, Request> Pipeline;
   
    struct Bank
    {
        bool     busy;
        Request  request;
        CycleNo  done;
        Pipeline incoming;
        Pipeline outgoing;
        
        Bank();
    };

    virtual size_t GetBankFromAddress(MemAddr address) const;
    static void PrintRequest(std::ostream& out, char prefix, const Request& request, CycleNo done);    
    void AddRequest(Pipeline& queue, const Request& request, bool data);

protected:    
    std::set<IMemoryCallback*>  m_caches;
    std::vector<Bank>           m_banks;
    CycleNo                     m_baseRequestTime;
    CycleNo                     m_timePerLine;
    size_t                      m_sizeOfLine;
    BufferSize                  m_bufferSize;
    size_t                      m_cachelineSize;
};

}
#endif

