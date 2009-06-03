#include "ParallelMemory.h"
#include "config.h"
#include <cassert>
#include <sstream>
#include <cstring>
using namespace Simulator;
using namespace std;

void ParallelMemory::Request::release()
{
    if (refcount != NULL && --*refcount == 0) {
        delete[] (char*)data.data;
        delete refcount;
    }
}

ParallelMemory::Request& ParallelMemory::Request::operator =(const Request& req)
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

ParallelMemory::Request::Request(const Request& req) : refcount(NULL) { *this = req; }
ParallelMemory::Request::Request() { refcount = new unsigned long(1); data.data = NULL; }
ParallelMemory::Request::~Request() { release(); }

                                    
void ParallelMemory::RegisterListener(IMemoryCallback& callback)
{
    m_caches.insert(&callback);
}

void ParallelMemory::UnregisterListener(IMemoryCallback& callback)
{
    m_caches.erase(&callback);
}

void ParallelMemory::AddRequest(Request& request)
{
	Port* port;
	map<IMemoryCallback*, Port*>::iterator p = m_portmap.find(request.callback);
	if (p == m_portmap.end())
	{
		// Port mapping doesn't exist yet, create it
		size_t index = m_portmap.size();
		assert(index < m_ports.size());
		port = &m_ports[index];

		m_portmap.insert(make_pair(request.callback, port));
	}
	else
	{
		port = p->second;
	}

	port->m_requests.push_back(request);
	m_numRequests++;
	m_statMaxRequests = max(m_statMaxRequests, m_numRequests);
}

Result ParallelMemory::Read(IMemoryCallback& callback, MemAddr address, 
                            void* /* data */, MemSize size, MemTag tag)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    if (m_bufferSize == INFINITE || m_numRequests < m_bufferSize)
    {
        COMMIT
        {
            Request request;
            request.callback  = &callback;
            request.address   = address;
            request.data.data = new char[ (size_t)size ];
            request.data.size = size;
            request.data.tag  = tag;
            request.write     = false;
            AddRequest(request);
        }
        return DELAYED;
    }
    return FAILED;
}

Result ParallelMemory::Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    if (m_bufferSize == INFINITE || m_numRequests < m_bufferSize)
    {
        Request request;
        request.callback  = &callback;
        request.address   = address;
        request.data.data = new char[ (size_t)size ];
        request.data.size = size;
        request.data.tag  = tag;
        request.write     = true;
        memcpy(request.data.data, data, (size_t)size);

        // Broadcast the snoop data
        for (set<IMemoryCallback*>::iterator p = m_caches.begin(); p != m_caches.end(); ++p)
        {
            if (!(*p)->OnMemorySnooped(request.address, request.data))
            {
                return FAILED;
            }
        }

        COMMIT{ AddRequest(request); }
        return DELAYED;
    }
    return FAILED;
}

bool ParallelMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
}

Result ParallelMemory::OnCycleWritePhase(unsigned int stateIndex)
{
	CycleNo now         = GetKernel()->GetCycleNo();
	Port&   port        = m_ports[stateIndex];
	size_t  nAvailable  = m_width - port.m_inFlight.size();
	Result  result      = DELAYED;

	// Check if the first pending request has completed
	if (!port.m_inFlight.empty())
	{
		multimap<CycleNo, Request>::iterator p = port.m_inFlight.begin();
		if (now >= p->first)
		{
			// Yes, do the callback
			const Request& request = p->second;
			
			if (request.write)
			{
				VirtualMemory::Write(request.address, request.data.data, request.data.size);
    			if (!request.callback->OnMemoryWriteCompleted(request.data.tag))
	    		{
		    		return FAILED;
			    }
			}
			else
			{
				VirtualMemory::Read(request.address, request.data.data, request.data.size);
			    if (!request.callback->OnMemoryReadCompleted(request.data))
    			{
    				return FAILED;
    			}
			}
			
			COMMIT
			{
				port.m_inFlight.erase(p);
				m_numRequests--;
			}

			nAvailable++;
		}

		// A request is still active
		result = SUCCESS;
	}

	size_t nDispatched = 0;
	for (deque<Request>::const_iterator p = port.m_requests.begin(); p != port.m_requests.end() && nDispatched < nAvailable; p++, nDispatched++)
	{
		// A new request can be handled
  		COMMIT
   		{
   			// Time the request
    		CycleNo delay = m_baseRequestTime + m_timePerLine * (p->data.size + m_sizeOfLine - 1) / m_sizeOfLine;
	    	port.m_inFlight.insert(make_pair(now + delay, *p));
    	}
	}

	COMMIT
	{
		// Remove the dispatched requests
		for (size_t i = 0; i < nDispatched; ++i)
		{
			port.m_requests.pop_front();
		}

		// Update stats
		m_statMaxInFlight = max(m_statMaxInFlight, port.m_inFlight.size());
	}
	return (nDispatched > 0) ? SUCCESS : result;
}

void ParallelMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    return VirtualMemory::Reserve(address, size, perm);
}

void ParallelMemory::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool ParallelMemory::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}
    
void ParallelMemory::Read (MemAddr address, void* data, MemSize size)
{
	return VirtualMemory::Read(address, data, size);
}

void ParallelMemory::Write(MemAddr address, const void* data, MemSize size)
{
	return VirtualMemory::Write(address, data, size);
}

static size_t GetNumProcessors(const Config& config)
{
    const vector<PSize> places = config.getIntegerList<PSize>("NumProcessors");
    PSize numProcs = 0;
    for (size_t i = 0; i < places.size(); ++i) {
        numProcs += places[i];
    }
    return numProcs;    
}

static string CreateStateNames(size_t numProcs)
{
    stringstream states;
    for (size_t i = 0; i < numProcs; ++i)
    {
        states << "port" << i << "|";
    }
    string ret = states.str();
    if (!ret.empty()) {
        ret.erase(ret.end() - 1, ret.end());
    }
    return ret;
}

ParallelMemory::ParallelMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config) :
    IComponent(parent, kernel, name, CreateStateNames(GetNumProcessors(config))),
    m_ports(GetNumProcessors(config)),
    m_bufferSize     (config.getInteger<BufferSize>("MemoryBufferSize", INFINITE)),
    m_baseRequestTime(config.getInteger<CycleNo>   ("MemoryBaseRequestTime", 1)),
    m_timePerLine    (config.getInteger<CycleNo>   ("MemoryTimePerLine", 1)),
    m_sizeOfLine     (config.getInteger<size_t>    ("MemorySizeOfLine", 8)),
    m_width          (config.getInteger<size_t>    ("MemoryParallelRequests", 1)),
    m_numRequests(0),
    m_statMaxRequests(0),
    m_statMaxInFlight(0)
{
}

ParallelMemory::~ParallelMemory()
{
}
