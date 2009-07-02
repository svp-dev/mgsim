#ifndef IDEALMEMORY_H
#define IDEALMEMORY_H

#include "Memory.h"
#include "buffer.h"
#include "VirtualMemory.h"
#include <deque>
#include <set>

class Config;

namespace Simulator
{

class IdealMemory : public IComponent, public IMemoryAdmin, public VirtualMemory
{
public:
    struct Request
    {
        bool                write;
        CycleNo             done;
        MemAddr             address;
        MemData             data;
        IMemoryCallback*    callback;
    };

    IdealMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config);

    CycleNo GetTotalWaitTime() const { return m_totalWaitTime; }

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
    std::set<IMemoryCallback*>  m_caches;
    std::deque<Request>         m_requests;
    BufferSize                  m_bufferSize;
    CycleNo                     m_baseRequestTime;
    CycleNo                     m_timePerLine;
    CycleNo                     m_sizeOfLine;
    CycleNo                     m_totalWaitTime;
};

}
#endif

