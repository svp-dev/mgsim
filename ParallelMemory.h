#ifndef PARALLELMEMORY_H
#define PARALLELMEMORY_H

#include <queue>
#include <deque>
#include <set>
#include <map>
#include <vector>
#include "Memory.h"
#include "kernel.h"
#include "Processor.h"

namespace Simulator
{

class ParallelMemory : public IComponent, public IMemory, public IMemoryAdmin
{
public:
	struct Config
	{
        MemSize    size;
        BufferSize bufferSize;
	    CycleNo	   baseRequestTime; // This many cycles per request regardless of size
        CycleNo	   timePerLine;     // With this many additional cycles per line
        size_t	   sizeOfLine;      // With this many bytes per line
	    size_t	   width;	        // number of requests which can be processed parallel
	};

    class Request
    {
        void release()
        {
            if (refcount != NULL && --*refcount == 0) {
                delete[] (char*)data.data;
                delete refcount;
            }
        }

    public:
        unsigned long*      refcount;
        bool                write;
        MemAddr             address;
        MemData             data;
        IMemoryCallback*    callback;

        Request& operator =(const Request& req)
        {
            release();
            refcount  = req.refcount;
            write     = req.write;
            address   = req.address;
            data      = req.data;
            callback  = req.callback;
            ++*refcount;
            return *this;
        }

        Request(const Request& req) : refcount(NULL) { *this = req; }
        Request() { refcount = new unsigned long(1); data.data = NULL; }
        ~Request() { release(); }
    };

	struct Port
	{
		std::deque<Request>             m_requests;
		std::multimap<CycleNo, Request> m_inFlight;
	};

    ParallelMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config, PSize numProcs );
    ~ParallelMemory();

    // Component
    Result onCycleWritePhase(int stateIndex);

    // IMemory
    void registerListener(IMemoryCallback& callback);
    void unregisterListener(IMemoryCallback& callback);
    Result read (IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);
    Result write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag);

    // IMemoryAdmin
    void read (MemAddr address, void* data, MemSize size);
    void write(MemAddr address, void* data, MemSize size);
    bool idle() const;

    const Config& getConfig()       const { return m_config; }
    const Port&   getPort(size_t i) const { return m_ports[i]; }
    size_t        getNumPorts()     const { return m_ports.size(); }

    size_t getStatMaxRequests() { return m_statMaxRequests; }
    size_t getStatMaxInFlight() { return m_statMaxInFlight; }
    
private:
    void addRequest(Request& request);
    
    std::set<IMemoryCallback*>        m_caches;
    std::map<IMemoryCallback*, Port*> m_portmap;
    std::vector<Port>                 m_ports;
 
    Config     m_config;
	BufferSize m_numRequests;
    char*      m_memory;
	size_t	   m_statMaxRequests;
	size_t	   m_statMaxInFlight;
};

}
#endif

