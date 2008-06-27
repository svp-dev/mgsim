#include "ParallelMemory.h"
#include <cassert>
#include <iostream>
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

                                    
void ParallelMemory::registerListener(IMemoryCallback& callback)
{
    m_caches.insert(&callback);
}

void ParallelMemory::unregisterListener(IMemoryCallback& callback)
{
    m_caches.erase(&callback);
}

void ParallelMemory::addRequest(Request& request)
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

Result ParallelMemory::read(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    if (m_config.bufferSize == INFINITE || m_numRequests < m_config.bufferSize)
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
            addRequest(request);
        }
        return DELAYED;
    }
    return FAILED;
}

Result ParallelMemory::write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    if (m_config.bufferSize == INFINITE || m_numRequests < m_config.bufferSize)
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
        for (set<IMemoryCallback*>::iterator p = m_caches.begin(); p != m_caches.end(); p++)
        {
            if (!(*p)->onMemorySnooped(request.address, request.data))
            {
                return FAILED;
            }
        }

        COMMIT{ addRequest(request); }
        return DELAYED;
    }
    return FAILED;
}

bool ParallelMemory::checkPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
}

Result ParallelMemory::onCycleWritePhase(unsigned int stateIndex)
{
	CycleNo now         = getKernel()->getCycleNo();
	Port&   port        = m_ports[stateIndex];
	size_t  nAvailable  = m_config.width - port.m_inFlight.size();
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
				VirtualMemory::write(request.address, request.data.data, request.data.size);
    			if (!request.callback->onMemoryWriteCompleted(request.data.tag))
	    		{
		    		return FAILED;
			    }
			    DebugSimWrite("Completed write to %llx\n", request.address);
			}
			else
			{
				VirtualMemory::read(request.address, request.data.data, request.data.size);
			    if (!request.callback->onMemoryReadCompleted(request.data))
    			{
    				return FAILED;
    			}
			    DebugSimWrite("Completed read to %llx\n", request.address);
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
    		CycleNo delay = m_config.baseRequestTime + m_config.timePerLine * (p->data.size + m_config.sizeOfLine - 1) / m_config.sizeOfLine;
	    	port.m_inFlight.insert(make_pair(now + delay, *p));
    	}
	}

	COMMIT
	{
		// Remove the dispatched requests
		for (size_t i = 0; i < nDispatched; i++)
		{
			port.m_requests.pop_front();
		}

		// Update stats
		m_statMaxInFlight = max(m_statMaxInFlight, port.m_inFlight.size());
	}
	return (nDispatched > 0) ? SUCCESS : result;
}

void ParallelMemory::read (MemAddr address, void* data, MemSize size)
{
	return VirtualMemory::read(address, data, size);
}

void ParallelMemory::write(MemAddr address, const void* data, MemSize size, int perm)
{
	return VirtualMemory::write(address, data, size, perm);
}

ParallelMemory::ParallelMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config, PSize numProcs ) :
    IComponent(parent, kernel, name, (int)numProcs),
    m_ports(numProcs),
    m_config(config),
    m_numRequests(0),
    m_statMaxRequests(0),
    m_statMaxInFlight(0)
{
}

ParallelMemory::~ParallelMemory()
{
}
