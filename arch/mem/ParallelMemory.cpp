#include <arch/mem/ParallelMemory.h>
#include <sim/config.h>

#include <cassert>
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
    WClientID        wid;
};

class ParallelMemory::Port : public Object
{
    ParallelMemory&     m_memory;
    IMemoryCallback&    m_callback;
    ArbitratedService<> p_requests;
    Buffer<Request>     m_requests;
    DefineStateVariable(CycleNo, nextdone);
    Process             p_Requests;
    size_t              m_lineSize;

    Result DoRequests()
    {
        assert(!m_requests.Empty());

        const Request& request = m_requests.Front();
        const CycleNo  now     = GetKernel()->GetActiveClock()->GetCycleNo();

        if (m_nextdone == 0)
        {
            // A new request is ready to be handled
            COMMIT
            {
                // Time the request
                CycleNo requestTime = m_memory.GetMemoryDelay(m_lineSize);
                m_nextdone = now + requestTime;
            }
        }
        // There is a request active
        else if (now >= m_nextdone)
        {
            // The current request has completed
            if (request.write)
            {
                static_cast<VirtualMemory&>(m_memory).Write(request.address, request.data.data, request.data.mask, m_lineSize);

                if (!m_callback.OnMemoryWriteCompleted(request.wid))
                {
                    return FAILED;
                }
            }
            else
            {
                char data[m_lineSize];

                static_cast<VirtualMemory&>(m_memory).Read(request.address, data, m_lineSize);

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
                out << obj->GetName();
            }
            out << endl;

            out << "      Address       | Type  " << endl;
            out << "--------------------+-------" << endl;

            for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
            {
                out << hex << setfill('0') << right
                    << " 0x" << setw(16) << p->address << " | ";

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

    bool OnMemorySnooped(MemAddr address, const char * data, const bool * mask)
    {
        return m_callback.OnMemorySnooped(address, data, mask);
    }

    Port(const std::string& name, ParallelMemory& memory, Clock& clock, BufferSize buffersize, IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, size_t lineSize)
        : Object(name, memory),
          m_memory(memory),
          m_callback(callback),
          p_requests(clock, GetName() + ".p_requests"),
          InitStorage(m_requests, clock, buffersize),
          InitStateVariable(nextdone, 0),
          InitProcess(p_Requests, DoRequests),
          m_lineSize(lineSize)
    {
        m_requests.Sensitive( p_Requests );
        p_requests.AddProcess(process);
        traces = m_requests;
        p_Requests.SetStorageTraces(opt(storages));
    }
};

// Time it takes to process (read/write) a request once arrived
CycleNo ParallelMemory::GetMemoryDelay(size_t data_size) const
{
    return m_baseRequestTime + m_timePerLine * (data_size + m_lineSize - 1) / m_lineSize;
}

MCID ParallelMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool /*ignored*/)
{
#ifndef NDEBUG
    for (auto p : m_ports) {
        assert(&p->GetCallback() != &callback);
    }
#endif

    RegisterModelRelation(callback.GetMemoryPeer(), *this, "mem");

    MCID id = m_ports.size();

    m_ports.push_back(new Port("port" + to_string(id), *this, m_clock, m_buffersize, callback, process, traces, storages, m_lineSize));

    return id;
}

void ParallelMemory::UnregisterClient(MCID id)
{
    assert(id < m_ports.size() && m_ports[id] != NULL);
    delete m_ports[id];
    m_ports[id] = NULL;
}

bool ParallelMemory::Read(MCID id, MemAddr address)
{
    assert(address % m_lineSize == 0);

    // Client should have registered
    assert(id < m_ports.size());

    Request request;
    request.address   = address;
    request.write     = false;

    if (!m_ports[id]->AddRequest(request))
    {
        return false;
    }

    COMMIT{ ++m_nreads; m_nread_bytes += m_lineSize; }
    return true;
}

bool ParallelMemory::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
    assert(address % m_lineSize == 0);

    // Client should have registered
    assert(id < m_ports.size());

    Request request;
    request.address   = address;
    request.wid       = wid;
    request.write     = true;
    COMMIT{
    std::copy(data.data, data.data + m_lineSize, request.data.data);
    std::copy(data.mask, data.mask + m_lineSize, request.data.mask);
    }

    // Broadcast the snoop data
    for (auto p : m_ports)
    {
        if (!p->OnMemorySnooped(request.address, request.data.data, request.data.mask))
        {
            return false;
        }
    }

    if (!m_ports[id]->AddRequest(request))
    {
        return false;
    }

    COMMIT { ++m_nwrites; m_nwrite_bytes += m_lineSize; }
    return true;
}

ParallelMemory::ParallelMemory(const std::string& name, Object& parent, Clock& clock) :
    VirtualMemory(name, parent),
    m_clock          (clock),
    m_ports          (),
    m_buffersize     (GetConf("BufferSize", BufferSize)),
    m_baseRequestTime(GetConf("BaseRequestTime", CycleNo)),
    m_timePerLine    (GetConf("TimePerLine", CycleNo)),
    m_lineSize       (GetTopConf("CacheLineSize", size_t)),
    InitSampleVariable(nreads, SVC_CUMULATIVE),
    InitSampleVariable(nread_bytes, SVC_CUMULATIVE),
    InitSampleVariable(nwrites, SVC_CUMULATIVE),
    InitSampleVariable(nwrite_bytes, SVC_CUMULATIVE)
{
    RegisterModelObject(*this, "pmem");

}

ParallelMemory::~ParallelMemory()
{
    for (auto p : m_ports)
        delete p;
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

    for (auto q : m_ports)
    {
        q->Print(out);
    }
}

}
