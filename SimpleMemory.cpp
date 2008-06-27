#include <cassert>
#include "SimpleMemory.h"
using namespace Simulator;
using namespace std;

void SimpleMemory::registerListener(IMemoryCallback& callback)
{
    m_caches.insert(&callback);
}

void SimpleMemory::unregisterListener(IMemoryCallback& callback)
{
    m_caches.erase(&callback);
}

Result SimpleMemory::read(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	if (m_config.bufferSize == INFINITE || m_requests.size() < m_config.bufferSize)
    {
        COMMIT
        {
            Request request;
            request.callback  = &callback;
            request.address   = address;
            request.data.data = new char[ (size_t)size ];
            request.data.size = size;
            request.data.tag  = tag;
            request.done      = 0;
            request.write     = false;
            m_requests.push(request);
        }
        return DELAYED;
    }
    return FAILED;
}

Result SimpleMemory::write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    if (m_config.bufferSize == INFINITE || m_requests.size() < m_config.bufferSize)
    {
		assert(tag.fid != INVALID_LFID);
        Request request;
        request.callback  = &callback;
        request.address   = address;
        request.data.data = new char[ (size_t)size ];
        request.data.size = size;
        request.data.tag  = tag;
        request.done      = 0;
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

        COMMIT{ m_requests.push(request); }
        return DELAYED;
    }
    return FAILED;
}

void SimpleMemory::read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::read(address, data, size);
}

void SimpleMemory::write(MemAddr address, const void* data, MemSize size)
{
	return VirtualMemory::write(address, data, size);
}

bool SimpleMemory::checkPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
}

Result SimpleMemory::onCycleWritePhase(unsigned int stateIndex)
{
	Result result = (!m_requests.empty()) ? SUCCESS : DELAYED;

    CycleNo now = getKernel()->getCycleNo();
    if (!m_requests.empty())
    {
        const Request& request = m_requests.front();
        if (request.done > 0 && now >= request.done)
        {
            // The current request has completed
            if (!request.write && !request.callback->onMemoryReadCompleted(request.data))
            {
                return FAILED;
            }

            if (request.write && !request.callback->onMemoryWriteCompleted(request.data.tag))
            {
                return FAILED;
            }

            COMMIT{ m_requests.pop(); }
        }
    }

    if (!m_requests.empty())
    {
        Request& request = m_requests.front();
        if (request.done == 0)
        {
            COMMIT
            {
                // A new request is ready to be handled
                if (request.write) {
					VirtualMemory::write(request.address, request.data.data, request.data.size);
                } else {
					VirtualMemory::read(request.address, request.data.data, request.data.size);
                }

                // Time the request
                CycleNo requestTime = m_config.baseRequestTime + m_config.timePerLine * (request.data.size + m_config.sizeOfLine - 1) / m_config.sizeOfLine;
                request.done = now + requestTime;
                m_totalWaitTime += requestTime;
            }
        }
    }
    return result;
}

SimpleMemory::SimpleMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config) :
    IComponent(parent, kernel, name), m_config(config), m_totalWaitTime(0)
{
}
