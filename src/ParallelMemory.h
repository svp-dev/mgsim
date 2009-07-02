#ifndef PARALLELMEMORY_H
#define PARALLELMEMORY_H

#include "Memory.h"
#include "kernel.h"
#include "Processor.h"
#include "VirtualMemory.h"
#include <queue>
#include <deque>
#include <set>
#include <map>
#include <vector>

namespace Simulator
{

class ParallelMemory : public IComponent, public IMemoryAdmin, public VirtualMemory
{
public:
    ParallelMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config);
    ~ParallelMemory();

    // Component
    Result OnCycleWritePhase(unsigned int stateIndex);

    // IMemory
    void   Reserve(MemAddr address, MemSize size, int perm);
    void   Unreserve(MemAddr address);
    void   RegisterListener(PSize pid, IMemoryCallback& callback);
    void   UnregisterListener(PSize pid, IMemoryCallback& callback);
    Result Read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
    Result Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
	bool   CheckPermissions(MemAddr address, MemSize size, int access) const;

    // IMemoryAdmin
    bool Allocate(MemSize size, int perm, MemAddr& address);
    void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);

    size_t GetStatMaxRequests() { return m_statMaxRequests; }
    size_t GetStatMaxInFlight() { return m_statMaxInFlight; }
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
private:
    struct Request
    {
        bool             write;
        MemAddr          address;
        MemData          data;
        IMemoryCallback* callback;
    };

	struct Port
	{
		std::deque<Request>             m_requests;
		std::multimap<CycleNo, Request> m_inFlight;
	};

    void AddRequest(Request& request);
    static void PrintRequest(std::ostream& out, const Request& request, CycleNo done);
    
    std::set<IMemoryCallback*>        m_caches;
    std::map<IMemoryCallback*, Port*> m_portmap;
    std::vector<Port>                 m_ports;
 
    BufferSize    m_bufferSize;
	CycleNo	      m_baseRequestTime; // Config: This many cycles per request regardless of size
    CycleNo	      m_timePerLine;     // Config: With this many additional cycles per line
    size_t	      m_sizeOfLine;      // Config: With this many bytes per line
	size_t	      m_width;	         // Config: Number of requests which can be processed parallel
	BufferSize    m_numRequests;
	size_t	      m_statMaxRequests;
	size_t	      m_statMaxInFlight;
};

}
#endif

