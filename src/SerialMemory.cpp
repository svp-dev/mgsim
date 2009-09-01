#include "SerialMemory.h"
#include "config.h"
#include <cassert>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

void SerialMemory::RegisterListener(PSize /*pid*/, IMemoryCallback& callback, const ArbitrationSource* sources)
{
    m_caches.insert(&callback);
    for (; sources->first != NULL; sources++)
    {
        p_requests.AddSource(*sources);
    }
}

void SerialMemory::UnregisterListener(PSize /*pid*/, IMemoryCallback& callback)
{
    m_caches.erase(&callback);
}

bool SerialMemory::Read(IMemoryCallback& callback, MemAddr address, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }
    
    if (!p_requests.Invoke())
    {
        return false;
    }

    Request request;
    request.callback  = &callback;
    request.address   = address;
    request.data.size = size;
    request.data.tag  = tag;
    request.write     = false;

    if (!m_requests.Push(request))
    {
        return false;
    }
    
    return true;
}

bool SerialMemory::Write(IMemoryCallback& callback, MemAddr address, const void* data, MemSize size, MemTag tag)
{
    assert(tag.fid != INVALID_LFID);

    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    if (!p_requests.Invoke())
    {
        return false;
    }

    Request request;
    request.callback  = &callback;
    request.address   = address;
    request.data.size = size;
    request.data.tag  = tag;
    request.write     = true;
    memcpy(request.data.data, data, (size_t)size);

    if (!m_requests.Push(request))
    {
        return false;
    }

    // Broadcast the snoop data
    for (set<IMemoryCallback*>::iterator p = m_caches.begin(); p != m_caches.end(); ++p)
    {
        if (!(*p)->OnMemorySnooped(request.address, request.data))
        {
            return false;
        }
    }

    return true;
}

void SerialMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    return VirtualMemory::Reserve(address, size, perm);
}

void SerialMemory::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool SerialMemory::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}

void SerialMemory::Read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::Read(address, data, size);
}

void SerialMemory::Write(MemAddr address, const void* data, MemSize size)
{
	return VirtualMemory::Write(address, data, size);
}

bool SerialMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
	return VirtualMemory::CheckPermissions(address, size, access);
}

Result SerialMemory::OnCycle(unsigned int /* stateIndex */)
{
    assert(!m_requests.Empty());

    const Request& request = m_requests.Front();
    const CycleNo  now     = GetKernel()->GetCycleNo();
        
    if (m_nextdone > 0)
    {
        // There is already a request active
        if (now >= m_nextdone)
        {
            // The current request has completed
            if (request.write) {
        		VirtualMemory::Write(request.address, request.data.data, request.data.size);
                if (!request.callback->OnMemoryWriteCompleted(request.data.tag))
                {
                    return FAILED;
                }
            } else {
                MemData data(request.data);
  		        VirtualMemory::Read(request.address, data.data, data.size);
                if (!request.callback->OnMemoryReadCompleted(data))
                {
                    return FAILED;
                }
            }
            m_requests.Pop();
            COMMIT{ m_nextdone = 0; }
        }
    }
    else
    {
        // A new request is ready to be handled
        COMMIT
        {
            // Time the request
            CycleNo requestTime = m_baseRequestTime + m_timePerLine * (request.data.size + m_sizeOfLine - 1) / m_sizeOfLine;
            m_nextdone = now + requestTime;
        }
    }
    return SUCCESS;
}

SerialMemory::SerialMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config) :
    IComponent(parent, kernel, name), 
    m_requests       (kernel, config.getInteger<BufferSize>("MemoryBufferSize", INFINITE)),
    p_requests       (*this, "m_requests"),
    m_baseRequestTime(config.getInteger<CycleNo>   ("MemoryBaseRequestTime", 1)),
    m_timePerLine    (config.getInteger<CycleNo>   ("MemoryTimePerLine", 1)),
    m_sizeOfLine     (config.getInteger<CycleNo>   ("MemorySizeOfLine", 8)),
    m_nextdone(0)
{
    m_requests.Sensitive(*this, 0);
}

void SerialMemory::Cmd_Help(ostream& out, const vector<string>& /*arguments*/) const
{
    out <<
    "The Serial Memory is a simplified memory implementation that has no contention\n"
    "on accesses and simply queues all requests in a single queue.\n\n"
    "Supported operations:\n"
    "- info <component>\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- read <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- read <component> requests\n"
    "  Reads the requests buffer\n";
}

void SerialMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }
    
    out << "      Address       | Size |  CID  |    Type    | Source" << endl;
    out << "--------------------+------+-------+------------+---------------------" << endl;

    for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
    {
        out << hex << setfill('0') << right 
            << " 0x" << setw(16) << p->address << " | "
            << setfill(' ') << setw(4) << dec << p->data.size << " | ";

        if (p->data.tag.cid == INVALID_CID) {
            out << " N/A  | ";
        } else {
            out << setw(5) << p->data.tag.cid << " | ";
        }

        if (p->write) {
            out << "Data write";
        } else if (p->data.tag.cid != INVALID_CID) {
            out << "Cache-line";
        } else {
            out << "Data read ";
        }
        out << " | ";

        Object* obj = dynamic_cast<Object*>(p->callback);
        if (obj == NULL) {
            out << "???";
        } else {
            out << obj->GetFQN();
        }

        out << endl;
    }

    out << endl << "First request done at: ";
    if (m_nextdone == 0) {
        out << "N/A";
    } else {
        out << dec << m_nextdone;
    }
    out << endl << endl;
}

}
