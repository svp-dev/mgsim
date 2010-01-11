#include "ParallelMemory.h"
#include "config.h"
#include <cassert>
#include <sstream>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

struct ParallelMemory::Request
{
    IMemoryCallback* callback;
    bool             write;
    MemAddr          address;
    MemData          data;
};

class ParallelMemory::Port : public Object
{
    ParallelMemory&  m_memory;
    Buffer<Request>  m_requests;
    CycleNo          m_nextdone;
    Process          p_Requests;

    Result DoRequests()
    {
        assert(!m_requests.Empty());
    
        const Request& request = m_requests.Front();
        const CycleNo  now     = GetKernel()->GetCycleNo();

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
                if (!request.callback->OnMemoryWriteCompleted(request.data.tag))
                {
                    return FAILED;
                }
            }
            else
            {
                MemData data(request.data);
                m_memory.Read(request.address, data.data, data.size);
                if (!request.callback->OnMemoryReadCompleted(data))
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
    void Print(ostream& out)
    {
        if (!m_requests.Empty())
        {
            out << "Port for: ";
            Object* obj = dynamic_cast<Object*>(m_requests.Front().callback);
            if (obj == NULL) {
                out << "???";
            } else {
                out << obj->GetFQN();
            }
            out << endl;
        
            out << "      Address       | Size |  CID  |    Type    " << endl;
            out << "--------------------+------+-------+------------" << endl;

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
        return m_requests.Push(request);
    }

    Port(const std::string& name, ParallelMemory& memory, BufferSize buffersize)
        : Object(name, memory),
          m_memory(memory),
          m_requests(*memory.GetKernel(), buffersize), m_nextdone(0),
          p_Requests("port", delegate::create<Port, &Port::DoRequests>(*this))
    {
        m_requests.Sensitive( p_Requests );
    }
};

struct ParallelMemory::ClientInfo
{
    IMemoryCallback* callback;
    Port*            port;
};

// Time it takes to process (read/write) a request once arrived
CycleNo ParallelMemory::GetMemoryDelay(size_t data_size) const
{
    return m_baseRequestTime + m_timePerLine * (data_size + m_sizeOfLine - 1) / m_sizeOfLine;
}

void ParallelMemory::RegisterClient(PSize pid, IMemoryCallback& callback, const Process* /*processes*/[])
{
    ClientInfo& client = m_clients[pid];
    assert(client.callback == NULL);
    client.callback = &callback;
}

void ParallelMemory::UnregisterClient(PSize pid)
{
    ClientInfo& client = m_clients[pid];
    assert(client.callback != NULL);
    client.callback = NULL;
}

bool ParallelMemory::Read(PSize pid, MemAddr address, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }
    
    // Client should have registered
    assert(m_clients[pid].callback != NULL);
    
    Request request;
    request.address   = address;
    request.callback  = m_clients[pid].callback;
    request.data.size = size;
    request.data.tag  = tag;
    request.write     = false;
    
    if (!m_clients[pid].port->AddRequest(request))
    {
        return false;
    }

    COMMIT{ ++m_nreads; m_nread_bytes += size; }
    return true;
}

bool ParallelMemory::Write(PSize pid, MemAddr address, const void* data, MemSize size, MemTag tag)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    // Client should have registered
    assert(m_clients[pid].callback != NULL);
    
    Request request;
    request.address   = address;
    request.callback  = m_clients[pid].callback;
    request.data.size = size;
    request.data.tag  = tag;
    request.write     = true;
    memcpy(request.data.data, data, (size_t)size);

    // Broadcast the snoop data
    for (std::vector<ClientInfo>::iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (!p->callback->OnMemorySnooped(request.address, request.data))
        {
            return false;
        }
    }

    if (!m_clients[pid].port->AddRequest(request))
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

ParallelMemory::ParallelMemory(const std::string& name, Object& parent, const Config& config) :
    Object(name, parent),
    m_baseRequestTime(config.getInteger<CycleNo>("MemoryBaseRequestTime", 1)),
    m_timePerLine    (config.getInteger<CycleNo>("MemoryTimePerLine", 1)),
    m_sizeOfLine     (config.getInteger<size_t> ("MemorySizeOfLine", 8)),
    m_nreads         (0),
    m_nread_bytes    (0),
    m_nwrites        (0),
    m_nwrite_bytes   (0)
{
    const BufferSize buffersize = config.getInteger<BufferSize>("MemoryBufferSize", INFINITE);

    // Get number of processors as number of ports
    const vector<PSize> places = config.getIntegerList<PSize>("NumProcessors");
    PSize numProcs = 0;
    for (size_t i = 0; i < places.size(); ++i) {
        numProcs += places[i];
    }
    m_clients.resize(numProcs);

    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        stringstream name;
        name << "port" << i;
        ClientInfo& client = m_clients[i];
        client.callback = NULL;
        client.port     = new Port(name.str(), *this, buffersize);
    }
}

ParallelMemory::~ParallelMemory()
{
    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        delete m_clients[i].port;
    }
}

void ParallelMemory::Cmd_Help(ostream& out, const vector<string>& /*arguments*/) const
{
    out <<
    "The Parallel Memory is an extension on the Ideal Memory in that every CPU has a\n"
    "dedicated port into memory. Internally, there are no conflicts between ports so\n"
    "every CPU can issue memory operations fully independent of all other CPUs.\n\n"
    "Supported operations:\n"
    "- info <component>\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- read <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- read <component> requests\n"
    "  Reads the ports' requests buffers\n";
}

void ParallelMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }

    for (std::vector<ClientInfo>::const_iterator q = m_clients.begin(); q != m_clients.end(); ++q)
    {
        q->port->Print(out);
    }
}

}
