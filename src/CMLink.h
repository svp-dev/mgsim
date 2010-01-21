#ifndef CACHELINK_H
#define CACHELINE_H

#include <queue>
#include <set>
#include "Memory.h"
#include "kernel.h"
#include "config.h"
#include "coma/simlink/linkmgs.h"
#include "Processor.h"

namespace Simulator
{

class CMLink : public Object, public IMemoryAdmin
{
public:
    struct Request
    {
        bool                write;
        CycleNo             done;
        MemAddr             address;
        MemData             data;
        IMemoryCallback*    callback;
        CycleNo             starttime;  // MESMX debug
        bool                bconflict;   
    };
        
    CMLink(const std::string& name, Object& parent, const Config& config, LinkMGS* linkmgs);

    void RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[]);
    void UnregisterClient(PSize pid);

    void Reserve(MemAddr address, MemSize size, int perm);
    void Unreserve(MemAddr address);

    bool Read (PSize pid, MemAddr address, MemSize size, MemTag tag);
    bool Write(PSize pid, MemAddr address, const void* data, MemSize size, MemTag tag);

    // IMemory
    // Result Read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
    // Result Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
	bool CheckPermissions(MemAddr address, MemSize size, int access) const;
	
	Process p_Requests;

    Result DoRequests();

    // IMemoryAdmin
    bool Allocate(MemSize size, int perm, MemAddr& address);
    void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);

    void GetMemoryStatistics(uint64_t&, uint64_t&, uint64_t&, uint64_t&) const;
    void SetProcessor(Processor* proc){m_pProcessor = proc;}
    Processor* GetProcessor(){return m_pProcessor;}

private:
    Buffer<Request>               m_requests;
    std::vector<IMemoryCallback*> m_clients;
    std::set<Request*>            m_setrequests;     // MESMX 
    unsigned int                  m_nTotalReq;
    LinkMGS*                      m_linkmgs;     // MESMX
    IMemoryCallback*              m_pimcallback;
    Processor*                    m_pProcessor;
};

}
#endif
