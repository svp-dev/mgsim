#include "ParallelMemory.h"
#include "sim/config.h"
#include <cassert>
#include <sstream>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

struct ParallelMemory::Request
{
    bool             write;
    MemAddr          address;
    MemData          data;
    TID              tid;
};

class ParallelMemory::Port : public Object
{
    ParallelMemory&     m_memory;
    IMemoryCallback&    m_callback;
    ArbitratedService<> p_requests;
    Buffer<Request>     m_requests;
    CycleNo             m_nextdone;
    Process             p_Requests;

    Result DoRequests()
    {
        assert(!m_requests.Empty());
    
        const Request& request = m_requests.Front();
        const CycleNo  now     = GetCycleNo();

        if (m_nextdone == 0)
        {
            // A new request is ready to be handled
            COMMIT
            {
                // Time the request
                CycleNo requestTime = m_memory.GetMemoryDelay(request.data.size);
                m_nextdone = now + requestTime;
            }
        }
        // There is a request active
        else if (now >= m_nextdone)
        {
            // The current request has completed
            if (request.write)
            {
                m_memory.Write(request.address, request.data.data, request.data.size);
                if (!m_callback.OnMemoryWriteCompleted(request.tid))
                {
                    return FAILED;
                }
            }
            else
            {
                MemData data(request.data);
                m_memory.Read(request.address, data.data, data.size);
                if (!m_callback.OnMemoryReadCompleted(request.address, data))
                {
                    return FAILED;
                }
            }
            m_requests.Pop();
            COMMIT{ m_nextdone = 0; }
        }
        return SUCCESS;
    }

public:
    IMemoryCallback& GetCallback()
    {
        return m_callback;
    }
    
    void Print(ostream& out)
    {
        if (!m_requests.Empty())
        {
            out << "Port for: ";
            Object* obj = dynamic_cast<Object*>(&m_callback);
            if (obj == NULL) {
                out << "???";
            } else {
                out << obj->GetFQN();
            }
            out << endl;
        
            out << "      Address       | Size | Type  " << endl;
            out << "--------------------+------+-------" << endl;

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
    
    bool AddRequest(const Request& request)
    {
        if (!p_requests.Invoke())
        {
            return false;
        }
        
        return m_requests.Push(request);
    }
    
    bool OnMemorySnooped(MemAddr address, const MemData& data)
    {
        return m_callback.OnMemorySnooped(address, data);
    }
    
    Port(const std::string& name, ParallelMemory& memory, BufferSize buffersize, IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage)
        : Object(name, memory),
          m_memory(memory),
          m_callback(callback),
          p_requests(*this, memory.GetClock(), "p_requests"),
          m_requests("b_requests", *this, memory.GetClock(), buffersize), m_nextdone(0),
          p_Requests(*this, "port", delegate::create<Port, &Port::DoRequests>(*this))
    {
        m_requests.Sensitive( p_Requests );
        p_requests.AddProcess(process);
        traces = m_requests;
        p_Requests.SetStorageTraces(opt(storage));
    }
};

// Time it takes to process (read/write) a request once arrived
CycleNo ParallelMemory::GetMemoryDelay(size_t data_size) const
{
    return m_baseRequestTime + m_timePerLine * (data_size + m_sizeOfLine - 1) / m_sizeOfLine;
}

MCID ParallelMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/)
{
#ifndef NDEBUG
    for (size_t i = 0; i < m_ports.size(); ++i) {
        assert(&m_ports[i]->GetCallback() != &callback);
    }
#endif

    m_registry.registerRelation(callback.GetMemoryPeer(), *this, "mem");

    MCID id = m_ports.size();

    stringstream name;
    name << "port" << id;
    m_ports.push_back(new Port(name.str(), *this, m_buffersize, callback, process, traces, storage));
    
    return id;
}

void ParallelMemory::UnregisterClient(MCID id)
{
    assert(id < m_ports.size() && m_ports[id] != NULL);
    delete m_ports[id];
    m_ports[id] = NULL;
}

bool ParallelMemory::Read(MCID id, MemAddr address, MemSize size)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }
    
    // Client should have registered
    assert(id < m_ports.size());
    
    Request request;
    request.address   = address;
    request.data.size = size;
    request.write     = false;
    
    if (!m_ports[id]->AddRequest(request))
    {
        return false;
    }

    COMMIT{ ++m_nreads; m_nread_bytes += size; }
    return true;
}

bool ParallelMemory::Write(MCID id, MemAddr address, const void* data, MemSize size, TID tid)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    // Client should have registered
    assert(id < m_ports.size());
    
    Request request;
    request.address   = address;
    request.data.size = size;
    request.tid       = tid;
    request.write     = true;
    memcpy(request.data.data, data, (size_t)size);

    // Broadcast the snoop data
    for (std::vector<Port*>::iterator p = m_ports.begin(); p != m_ports.end(); ++p)
    {
        if (!(*p)->OnMemorySnooped(request.address, request.data))
        {
            return false;
        }
    }

    if (!m_ports[id]->AddRequest(request))
    {
        return false;
    }

    COMMIT { ++m_nwrites; m_nwrite_bytes += size; }
    return true;
}

bool ParallelMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    return VirtualMemory::CheckPermissions(address, size, access);
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

ParallelMemory::ParallelMemory(const std::string& name, Object& parent, Clock& clock, Config& config) :
    Object(name, parent, clock),
    m_registry       (config),
    m_buffersize     (config.getValue<BufferSize>(*this, "BufferSize")),
    m_baseRequestTime(config.getValue<CycleNo>(*this, "BaseRequestTime")),
    m_timePerLine    (config.getValue<CycleNo>(*this, "TimePerLine")),
    m_sizeOfLine     (config.getValue<size_t> (*this, "LineSize")),
    m_nreads         (0),
    m_nread_bytes    (0),
    m_nwrites        (0),
    m_nwrite_bytes   (0)
{
    config.registerObject(*this, "pmem");

    RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nread_bytes, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrite_bytes, SVC_CUMULATIVE);
}

ParallelMemory::~ParallelMemory()
{
    for (size_t i = 0; i < m_ports.size(); ++i)
    {
        delete m_ports[i];
    }
}

void ParallelMemory::Cmd_Info(ostream& out, const vector<string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "ranges")
    {
        return VirtualMemory::Cmd_Info(out, arguments);
    }
    out <<
    "The Parallel Memory is an extension on the Ideal Memory in that every CPU has a\n"
    "dedicated port into memory. Internally, there are no conflicts between ports so\n"
    "every CPU can issue memory operations fully independent of all other CPUs.\n\n"
    "Supported operations:\n"
    "- info <component> ranges\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- inspect <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- inspect <component> requests\n"
    "  Reads the ports' requests buffers\n";
}

void ParallelMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }

    for (std::vector<Port*>::const_iterator q = m_ports.begin(); q != m_ports.end(); ++q)
    {
        (*q)->Print(out);
    }
}

}
