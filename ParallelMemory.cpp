#include "ParallelMemory.h"
#include <cassert>
#include <iostream>
using namespace Simulator;
using namespace std;

bool ParallelMemory::idle() const
{
    return m_numRequests == 0;
}

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

		m_portmap.insert(p, make_pair(request.callback, port));
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

    if (address + size > m_config.size)
    {
        throw InvalidArgumentException("Reading outside of memory");
    }

    if (m_config.bufferSize == INFINITE || m_numRequests < m_config.bufferSize)
    {
        COMMIT
        (
            Request request;
            request.callback  = &callback;
            request.address   = address;
            request.data.data = new char[ (size_t)size ];
            request.data.size = size;
            request.data.tag  = tag;
            request.write     = false;
            addRequest(request);
        )
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

    if (address + size > m_config.size)
    {
        throw InvalidArgumentException("Writing outside of memory");
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

        COMMIT( addRequest(request); )
        return DELAYED;
    }
    return FAILED;
}

void ParallelMemory::read(MemAddr address, void* data, MemSize size)
{
    // Admin function; never called from context of simulation; no 
    // need for COMMIT() test
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    memcpy(data, &m_memory[address], (size_t)size);
}

void ParallelMemory::write(MemAddr address, void* data, MemSize size)
{
    // Admin function; never called from context of simulation; no 
    // need for COMMIT() test
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    memcpy(&m_memory[address], data, (size_t)size);
}

Result ParallelMemory::onCycleWritePhase(int stateIndex)
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
			if (!request.write && !request.callback->onMemoryReadCompleted(request.data))
			{
				return FAILED;
			}
			else if (request.write && !request.callback->onMemoryWriteCompleted(request.data.tag))
			{
				return FAILED;
			}

			COMMIT
			(
				port.m_inFlight.erase(p);
				m_numRequests--;
			)

			nAvailable++;
		}

		// A request is still active
		result = SUCCESS;
	}

	size_t nDispatched = 0;
	for (deque<Request>::const_iterator p = port.m_requests.begin(); p != port.m_requests.end() && nDispatched < nAvailable; p++, nDispatched++)
	{
		COMMIT
		(
			// A new request can be handled
			// Time the request
			CycleNo done = now + m_config.baseRequestTime + m_config.timePerLine * (p->data.size + m_config.sizeOfLine - 1) / m_config.sizeOfLine;

			Request& request = port.m_inFlight.insert(make_pair(done, Request()))->second;

			request = *p;
			if (request.write) {
				memcpy(&m_memory[request.address], request.data.data, (size_t)request.data.size);
			} else {
				memcpy(request.data.data, &m_memory[request.address], (size_t)request.data.size);
			}
		)
	}

	COMMIT
	(
		// Remove the dispatched requests
		for (size_t i = 0; i < nDispatched; i++)
		{
			port.m_requests.pop_front();
		}

		// Update stats
		m_statMaxInFlight = max(m_statMaxInFlight, port.m_inFlight.size());
	)
	return (nDispatched > 0) ? SUCCESS : result;
}

ParallelMemory::ParallelMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config, PSize numProcs ) :
    IComponent(parent, kernel, name, (int)numProcs),
    m_ports(numProcs),
    m_config(config),
    m_numRequests(0),
    m_statMaxRequests(0),
    m_statMaxInFlight(0)
{
    if (config.size > SIZE_MAX)
    {
        throw InvalidArgumentException("Memory size too big");
    }
    m_memory = new char[ (size_t)config.size ];
}

ParallelMemory::~ParallelMemory()
{
    delete[] m_memory;
}
