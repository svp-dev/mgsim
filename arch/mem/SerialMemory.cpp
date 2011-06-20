#include "SerialMemory.h"
#include "sim/config.h"
#include <cassert>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

MCID SerialMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/)
{
    assert(std::find(m_clients.begin(), m_clients.end(), &callback) == m_clients.end());
    m_clients.push_back(&callback);
    
    p_requests.AddProcess(process);
    traces = m_requests;
    
    m_storages = m_storages ^ storage;
    p_Requests.SetStorageTraces(m_storages);
    
    m_registry.registerRelation(callback.GetMemoryPeer(), *this, "mem");
    
    return m_clients.size() - 1;
}

void SerialMemory::UnregisterClient(MCID id)
{
    assert(id < m_clients.size() && m_clients[id] != NULL);
    m_clients[id] = NULL;
}

bool SerialMemory::Read(MCID id, MemAddr address, MemSize size)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }
    
    if (!p_requests.Invoke())
    {
        return false;
    }

    // Client should have registered
    assert(id < m_clients.size() && m_clients[id] != NULL);
    
    Request request;
    request.callback  = m_clients[id];
    request.address   = address;
    request.data.size = size;
    request.write     = false;

    if (!m_requests.Push(request))
    {
        return false;
    }
    
    return true;
}

bool SerialMemory::Write(MCID id, MemAddr address, const void* data, MemSize size, TID tid)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    if (!p_requests.Invoke())
    {
        return false;
    }

    // Client should have registered
    assert(id < m_clients.size() && m_clients[id] != NULL);
    
    Request request;
    request.callback  = m_clients[id];
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
    m_registry       (config),
    m_clock          (clock),
    m_requests       ("b_requests", *this, clock, config.getValue<BufferSize>(*this, "BufferSize")),
    p_requests       (*this, clock, "m_requests"),
    m_baseRequestTime(config.getValue<CycleNo>   (*this, "BaseRequestTime")),
    m_timePerLine    (config.getValue<CycleNo>   (*this, "TimePerLine")),
    m_sizeOfLine     (config.getValue<CycleNo>   (*this, "LineSize")),
    m_nextdone(0),
    m_nreads(0),
    m_nread_bytes(0),
    m_nwrites(0),
    m_nwrite_bytes(0),
    
    p_Requests(*this, "requests", delegate::create<SerialMemory, &SerialMemory::DoRequests>(*this) )
{
    m_requests.Sensitive( p_Requests );
    config.registerObject(*this, "sermem");
    
    m_storages = StorageTraceSet(StorageTrace());   // Request handler is waiting for completion
    p_Requests.SetStorageTraces(m_storages);

    RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nread_bytes, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrite_bytes, SVC_CUMULATIVE);
}

void SerialMemory::Cmd_Info(ostream& out, const vector<string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "ranges")
    {
        return VirtualMemory::Cmd_Info(out, arguments);
    }
    out <<
    "The Serial Memory is a simplified memory implementation that has no contention\n"
    "on accesses and simply queues all requests in a single queue.\n\n"
    "Supported operations:\n"
    "- info <component> ranges\n"
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
