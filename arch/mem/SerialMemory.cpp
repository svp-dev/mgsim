#include "SerialMemory.h"
#include "sim/config.h"
#include <cassert>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

void SerialMemory::RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[])
{
    assert(m_clients[pid] == NULL);
    m_clients[pid] = &callback;
    
    for (size_t i = 0; processes[i] != NULL; ++i)
    {
        p_requests.AddProcess(*processes[i]);
    }
}

void SerialMemory::UnregisterClient(PSize pid)
{
    assert(m_clients[pid] != NULL);
    m_clients[pid] = NULL;
}

bool SerialMemory::Read(PSize pid, MemAddr address, MemSize size)
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
    request.callback  = m_clients[pid];
    request.address   = address;
    request.data.size = size;
    request.write     = false;

    if (!m_requests.Push(request))
    {
        return false;
    }
    
    return true;
}

bool SerialMemory::Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
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
    request.callback  = m_clients[pid];
    request.address   = address;
    request.data.size = size;
    request.tid       = tid;
    request.write     = true;
    memcpy(request.data.data, data, (size_t)size);

    if (!m_requests.Push(request))
    {
        return false;
    }

    // Broadcast the snoop data
    for (vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemorySnooped(request.address, request.data))
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

Result SerialMemory::DoRequests()
{
    assert(!m_requests.Empty());

    const Request& request = m_requests.Front();
    const CycleNo  now     = GetCycleNo();
        
    if (m_nextdone > 0)
    {
        // There is already a request active
        if (now >= m_nextdone)
        {
            // The current request has completed
            if (request.write) {
                VirtualMemory::Write(request.address, request.data.data, request.data.size);
                if (!request.callback->OnMemoryWriteCompleted(request.tid))
                {
                    return FAILED;
                }
                COMMIT {
                    ++m_nwrites;
                    m_nwrite_bytes += request.data.size;
                }
            } else {
                MemData data(request.data);
                VirtualMemory::Read(request.address, data.data, data.size);
                if (!request.callback->OnMemoryReadCompleted(request.address, data))
                {
                    return FAILED;
                }
                COMMIT {
                    ++m_nreads;
                    m_nread_bytes += request.data.size;
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

SerialMemory::SerialMemory(const std::string& name, Object& parent, Clock& clock, Config& config) :
    Object(name, parent, clock),
    m_clock          (clock),
    m_clients        (config.getValue<size_t>(*this, "NumClients",
                                              config.getValue<size_t>("NumProcessors", 0)), NULL),
    m_requests       ("b_requests", *this, clock, config.getValue<BufferSize>(*this, "BufferSize", 0)),
    p_requests       (*this, clock, "m_requests"),
    m_baseRequestTime(config.getValue<CycleNo>   (*this, "BaseRequestTime", 0)),
    m_timePerLine    (config.getValue<CycleNo>   (*this, "TimePerLine", 0)),
    m_sizeOfLine     (config.getValue<CycleNo>   (*this, "LineSize", 0)),
    m_nextdone(0),
    m_nreads(0),
    m_nread_bytes(0),
    m_nwrites(0),
    m_nwrite_bytes(0),
    
    p_Requests("requests", delegate::create<SerialMemory, &SerialMemory::DoRequests>(*this) )
{
    m_requests.Sensitive( p_Requests );
}

void SerialMemory::Cmd_Info(ostream& out, const vector<string>& /*arguments*/) const
{
    out <<
    "The Serial Memory is a simplified memory implementation that has no contention\n"
    "on accesses and simply queues all requests in a single queue.\n\n"
    "Supported operations:\n"
    "- info <component>\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- inspect <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- inspect <component> requests\n"
    "  Reads the requests buffer\n";
}

void SerialMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }
    
    out << "      Address       | Size | Type  | Source" << endl;
    out << "--------------------+------+-------+---------------------" << endl;

    for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
    {
        out << hex << setfill('0') << right 
            << " 0x" << setw(16) << p->address << " | "
            << setfill(' ') << setw(4) << dec << p->data.size << " | ";

        if (p->write) {
            out << "Write";
        } else {
            out << "Read ";
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
