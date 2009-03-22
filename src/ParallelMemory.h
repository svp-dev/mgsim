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

class ParallelMemory : public IComponent, public IMemory, public IMemoryAdmin, public VirtualMemory
{
public:
	struct Config
	{
        BufferSize bufferSize;
	    CycleNo	   baseRequestTime; // This many cycles per request regardless of size
        CycleNo	   timePerLine;     // With this many additional cycles per line
        size_t	   sizeOfLine;      // With this many bytes per line
	    size_t	   width;	        // number of requests which can be processed parallel
	};
	
    class Request
    {
        void release();
     public:
        unsigned long*   refcount;
        bool             write;
        MemAddr          address;
        MemData          data;
        IMemoryCallback* callback;

        Request& operator =(const Request& req);
        Request(const Request& req);
        Request();
        ~Request();
    };

	struct Port
	{
		std::deque<Request>             m_requests;
		std::multimap<CycleNo, Request> m_inFlight;
	};

    ParallelMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config, PSize numProcs );
    ~ParallelMemory();

    // Component
    Result OnCycleWritePhase(unsigned int stateIndex);

    // IMemory
    void   Reserve(MemAddr address, MemSize size, int perm);
    void   Unreserve(MemAddr address);
    void   RegisterListener(IMemoryCallback& callback);
    void   UnregisterListener(IMemoryCallback& callback);
    Result Read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
    Result Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
	bool   CheckPermissions(MemAddr address, MemSize size, int access) const;

    // IMemoryAdmin
	void Read (MemAddr address, void* data, MemSize size);
    void Write(MemAddr address, const void* data, MemSize size);

    const Config& GetConfig()       const { return m_config; }
    const Port&   GetPort(size_t i) const { return m_ports[i]; }
    size_t        GetNumPorts()     const { return m_ports.size(); }

    size_t GetStatMaxRequests() { return m_statMaxRequests; }
    size_t GetStatMaxInFlight() { return m_statMaxInFlight; }
    
private:
    void AddRequest(Request& request);
    
    std::set<IMemoryCallback*>        m_caches;
    std::map<IMemoryCallback*, Port*> m_portmap;
    std::vector<Port>                 m_ports;
 
    Config        m_config;
	BufferSize    m_numRequests;
	size_t	      m_statMaxRequests;
	size_t	      m_statMaxInFlight;
};

}
#endif

