#include "IdealMemory.h"
#include "config.h"
#include <cassert>
#include <cstring>
using namespace Simulator;
using namespace std;

void IdealMemory::RegisterListener(IMemoryCallback& callback)
{
    m_caches.insert(&callback);
}

void IdealMemory::UnregisterListener(IMemoryCallback& callback)
{
    m_caches.erase(&callback);
}

Result IdealMemory::Read(IMemoryCallback& callback, MemAddr address, 
                          void* /* data */, MemSize size, MemTag tag)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

	if (m_bufferSize == INFINITE || m_requests.size() < m_bufferSize)
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

Result IdealMemory::Write(IMemoryCallback& callback, MemAddr address, void* data, MemSize size, MemTag tag)
{
#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    if (m_bufferSize == INFINITE || m_requests.size() < m_bufferSize)
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
        for (set<IMemoryCallback*>::iterator p = m_caches.begin(); p != m_caches.end(); ++p)
        {
            if (!(*p)->OnMemorySnooped(request.address, request.data))
            {
                return FAILED;
            }
        }

        COMMIT{ m_requests.push(request); }
        return DELAYED;
    }
    return FAILED;
}

void IdealMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    return VirtualMemory::Reserve(address, size, perm);
}

void IdealMemory::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool IdealMemory::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}

void IdealMemory::Read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::Read(address, data, size);
}

void IdealMemory::Write(MemAddr address, const void* data, MemSize size)
{
	return VirtualMemory::Write(address, data, size);
}

bool IdealMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
}

Result IdealMemory::OnCycleWritePhase(unsigned int /* stateIndex */)
{
	Result result = (!m_requests.empty()) ? SUCCESS : DELAYED;

    CycleNo now = GetKernel()->GetCycleNo();
    if (!m_requests.empty())
    {
        const Request& request = m_requests.front();
        if (request.done > 0 && now >= request.done)
        {
            // The current request has completed
            if (!request.write && !request.callback->OnMemoryReadCompleted(request.data))
            {
                return FAILED;
            }

            if (request.write && !request.callback->OnMemoryWriteCompleted(request.data.tag))
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
					VirtualMemory::Write(request.address, request.data.data, request.data.size);
                } else {
					VirtualMemory::Read(request.address, request.data.data, request.data.size);
                }

                // Time the request
                CycleNo requestTime = m_baseRequestTime + m_timePerLine * (request.data.size + m_sizeOfLine - 1) / m_sizeOfLine;
                request.done = now + requestTime;
                m_totalWaitTime += requestTime;
            }
        }
    }
    return result;
}

IdealMemory::IdealMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config) :
    IComponent(parent, kernel, name), 
    m_bufferSize     (config.getInteger<BufferSize>("MemoryBufferSize", INFINITE)),
    m_baseRequestTime(config.getInteger<CycleNo>   ("MemoryBaseRequestTime", 1)),
    m_timePerLine    (config.getInteger<CycleNo>   ("MemoryTimePerLine", 1)),
    m_sizeOfLine     (config.getInteger<CycleNo>   ("MemorySizeOfLine", 8)),
    m_totalWaitTime(0)
{
}
