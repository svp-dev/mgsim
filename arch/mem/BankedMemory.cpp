#include "BankedMemory.h"
#include "sim/config.h"
#include <sstream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

struct BankedMemory::ClientInfo
{
    IMemoryCallback*     callback;
    ArbitratedService<>* service;
};

struct BankedMemory::Request
{
    ClientInfo* client;
    bool        write;
    MemAddr     address;
    MemData     data;
    TID         tid;
    CycleNo     done;
};

class BankedMemory::Bank : public Object
{
    BankedMemory&       m_memory;
    ArbitratedService<> p_incoming;
    Buffer<Request>     m_incoming;
    Buffer<Request>     m_outgoing;
    SingleFlag          m_busy;
    Request             m_request;
    Process             p_Incoming;
    Process             p_Outgoing;
    Process             p_Bank;
    
    bool AddRequest(Buffer<Request>& queue, Request& request, bool data)
    {
        COMMIT
        {
            const std::pair<CycleNo, CycleNo> delay = m_memory.GetMessageDelay(data ? request.data.size : 0);
            const CycleNo                     now   = GetCycleNo();
            
            // Get the arrival time of the first bits
            request.done = now + delay.first;
    
            Buffer<Request>::const_reverse_iterator p = queue.rbegin();
            if (p != queue.rend() && request.done < p->done)
            {
                // We're waiting for another message to be sent, so skip the initial delay
                // (pretend our data comes after the data of the previous message)
                request.done = p->done;
            }
    
            // Add the time it takes the message body to traverse the network
            request.done += delay.second;
        }
    
        if (!queue.Push(request))
        {
            return false;
        }
        return true;
    }
    
    Result DoIncoming()
    {
        // Handle incoming requests
        assert(!m_incoming.Empty());
        
        const CycleNo  now     = GetCycleNo();
        const Request& request = m_incoming.Front();
        if (now >= request.done)
        {
            // This request has arrived, process it
            if (m_busy.IsSet())
            {
                return FAILED;
            }
                    
            m_request = request;
            m_request.done = now + m_memory.GetMemoryDelay(request.data.size);
            if (!m_busy.Set())
            {
                return FAILED;
            }
            
            m_incoming.Pop();
        }
        return SUCCESS;
    }

    Result DoOutgoing()
    {
        // Handle outgoing requests
        assert(!m_outgoing.Empty());
        
        const CycleNo  now     = GetCycleNo();
        const Request& request = m_outgoing.Front();
        if (now >= request.done)
        {
            // This request has arrived, send it to the callback
            if (!request.client->service->Invoke())
            {
                return FAILED;
            }
                
            if (request.write) {
                if (!request.client->callback->OnMemoryWriteCompleted(request.tid)) {
                    return FAILED;
                }
            } else {
                if (!request.client->callback->OnMemoryReadCompleted(request.address, request.data)) {
                    return FAILED;
                }
            }
            
            m_outgoing.Pop();
        }
        return SUCCESS;
    }
    
    Result DoRequest()
    {
        // Process the bank itself
        assert(m_busy.IsSet());
        if (GetCycleNo() >= m_request.done)
        {
            // This bank is done serving the request
            if (m_request.write) {
                m_memory.Write(m_request.address, m_request.data.data, m_request.data.size);
            } else {
                m_memory.Read(m_request.address, m_request.data.data, m_request.data.size);
            }

            // Move it to the outgoing queue
            if (!AddRequest(m_outgoing, m_request, !m_request.write))
            {
                return FAILED;
            }
            
            if (!m_busy.Clear())
            {
                return FAILED;
            }
        }
        return SUCCESS;
    }

    static void PrintRequest(ostream& out, char prefix, const Request& request)
    {
        out << prefix << " "
            << hex << setfill('0') << right
            << " 0x" << setw(16) << request.address << " | "
            << setfill(' ') << setw(4) << dec << request.data.size << " | ";

        if (request.write) {
            out << "Write";
        } else { 
            out << "Read ";
        }
        out << " | " << setw(8) << dec << request.done << " | ";
    
        Object* obj = dynamic_cast<Object*>(request.client->callback);
        if (obj == NULL) {
            out << "???";
        } else {
            out << obj->GetFQN();
        }
        out << endl;
    }

public:
    void RegisterClient(ArbitratedService<>& client_arbitrator, const Process* processes[])
    {
        for (size_t i = 0; processes[i] != NULL; ++i)
        {
            p_incoming.AddProcess(*processes[i]);
        }
        client_arbitrator.AddProcess(p_Outgoing);
    }
    
    bool AddIncomingRequest(Request& request)
    {
        if (!p_incoming.Invoke())
        {
            return false;
        }
    
        return AddRequest(m_incoming, request, request.write);
    }

    bool HasRequests(void) const
    {
        return !(m_incoming.Empty() && m_outgoing.Empty() && !m_busy.IsSet());
    }

    void Print(ostream& out)
    {
        out << GetName() << ":" << endl;
        out << "        Address       | Size | Type  |   Done   | Source" << endl;
        out << "----------------------+------+-------+----------+----------------" << endl;

        for (Buffer<Request>::const_reverse_iterator p = m_incoming.rbegin(); p != m_incoming.rend(); ++p)
        {
            PrintRequest(out, '>', *p);
        }
        if (m_busy.IsSet()) {
            PrintRequest(out, '*', m_request);
        } else {
            out << "*                     |      |       |          |" << endl;
        }
        for (Buffer<Request>::const_reverse_iterator p = m_outgoing.rbegin(); p != m_outgoing.rend(); ++p)
        {
            PrintRequest(out, '<', *p);
        }
        out << endl;
    }
    
    Bank(const std::string& name, BankedMemory& memory, Clock& clock, BufferSize buffersize)
        : Object(name, memory, clock),
          m_memory  (memory),
          p_incoming(memory, clock, name + ".incoming"),
          m_incoming("b_incoming", *this, clock, buffersize),
          m_outgoing("b_outgoing", *this, clock, buffersize),
          m_busy    ("f_busy", *this, clock, false),
          p_Incoming(*this, "in",   delegate::create<Bank, &Bank::DoIncoming>(*this)),
          p_Outgoing(*this, "out",  delegate::create<Bank, &Bank::DoOutgoing>(*this)),
          p_Bank    (*this, "bank", delegate::create<Bank, &Bank::DoRequest> (*this))
    {
        m_incoming.Sensitive( p_Incoming );
        m_outgoing.Sensitive( p_Outgoing );
        m_busy    .Sensitive( p_Bank );
    }
};

// Time it takes for a message to traverse the memory <-> CPU network
std::pair<CycleNo, CycleNo> BankedMemory::GetMessageDelay(size_t body_size) const
{
    // Initial delay is log(N)
    // Body delay depends on size, and one cycle for the header
    return make_pair(
        (CycleNo)(log((double)m_banks.size()) / log(2.0)),
        (body_size + m_sizeOfLine - 1) / m_sizeOfLine + 1
    );
}

// Time it takes to process (read/write) a request once arrived
CycleNo BankedMemory::GetMemoryDelay(size_t data_size) const
{
    return m_baseRequestTime + m_timePerLine * (data_size + m_sizeOfLine - 1) / m_sizeOfLine;
}
                        
void BankedMemory::RegisterClient(PSize pid, IMemoryCallback& callback, const Process* processes[])
{
    ClientInfo& client = m_clients[pid];
    assert(client.callback == NULL);

    stringstream name;
    name << "client" << pid;
    client.service = new ArbitratedService<>(*this, m_clock, name.str());
    client.callback = &callback;
        
    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        m_banks[i]->RegisterClient(*client.service, processes);
    }

    m_registry.registerRelation(callback, *this, "mem");
}

void BankedMemory::UnregisterClient(PSize pid)
{
    ClientInfo& client = m_clients[pid];
    assert(client.callback != NULL);
    delete client.service;
    client.callback = NULL;
}

size_t BankedMemory::GetBankFromAddress(MemAddr address) const
{
    // We work on whole cache lines
    return (size_t)((address / m_cachelineSize) % m_banks.size());
}

bool BankedMemory::Read(PSize pid, MemAddr address, MemSize size)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    // Client should have been registered
    assert(m_clients[pid].callback != NULL);

    Request request;
    request.address   = address;
    request.client    = &m_clients[pid];
    request.data.size = size;
    request.write     = false;
    
    Bank& bank = *m_banks[ GetBankFromAddress(address) ];
    if (!bank.AddIncomingRequest(request))
    {
        return false;
    }

    COMMIT { ++m_nreads; m_nread_bytes += size; }
    return true;
}

bool BankedMemory::Write(PSize pid, MemAddr address, const void* data, MemSize size, TID tid)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }
    
    // Client should have been registered
    assert(m_clients[pid].callback != NULL);

    Request request;
    request.address   = address;
    request.client    = &m_clients[pid];
    request.data.size = size;
    request.tid       = tid;
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
    
    Bank& bank = *m_banks[ GetBankFromAddress(address) ];
    if (!bank.AddIncomingRequest(request))
    {
        return false;
    }

    COMMIT { ++m_nwrites; m_nwrite_bytes += size; }
    return true;
}

void BankedMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    return VirtualMemory::Reserve(address, size, perm);
}

void BankedMemory::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool BankedMemory::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}

void BankedMemory::Read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::Read(address, data, size);
}

void BankedMemory::Write(MemAddr address, const void* data, MemSize size)
{
    return VirtualMemory::Write(address, data, size);
}

bool BankedMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    return VirtualMemory::CheckPermissions(address, size, access);
}

BankedMemory::BankedMemory(const std::string& name, Object& parent, Clock& clock, Config& config) :
    Object(name, parent, clock),
    m_registry(config),
    m_clock(clock),
    m_clients        (config.getValue<size_t>("NumClients", 
                                              config.getValue<size_t>("NumProcessors"))),
    m_banks          (config.getValue<size_t>(*this, "NumBanks", 
                                              config.getValue<size_t>("NumProcessors"))),
    m_baseRequestTime(config.getValue<CycleNo>(*this, "BaseRequestTime")),
    m_timePerLine    (config.getValue<CycleNo>(*this, "TimePerLine")),
    m_sizeOfLine     (config.getValue<size_t> (*this, "LineSize")),
    m_cachelineSize  (config.getValue<size_t> ("CacheLineSize")),
    m_nreads         (0),
    m_nread_bytes    (0),
    m_nwrites        (0),
    m_nwrite_bytes   (0)
{
    const BufferSize buffersize = config.getValue<BufferSize>(*this, "BufferSize");
    
    // Initialize client info
    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        m_clients[i].callback = NULL;
    }

    config.registerObject(*this, "bmem");
        
    // Create the banks   
    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        stringstream name;
        name << "bank" << i;
        m_banks[i] = new Bank(name.str(), *this, clock, buffersize);

        config.registerObject(*m_banks[i], "bank");
        config.registerRelation(*this, *m_banks[i], "bank");
    }
}

BankedMemory::~BankedMemory()
{
    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        delete m_banks[i];
    }
}

void BankedMemory::Cmd_Info(ostream& out, const vector<string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "ranges")
    {
        return VirtualMemory::Cmd_Info(out, arguments);
    }
    out <<
    "The Banked Memory represents a switched memory network between P processors and N\n"
    "memory banks. Requests are sequentialized on each bank and the cache line-to-bank\n"
    "mapping is a simple modulo.\n\n"
    "Supported operations:\n"
    "- info <component> ranges\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- inspect <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- inspect <component> requests\n"
    "  Reads the banks' requests buffers and queues\n";
}

void BankedMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }

    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        if (m_banks[i]->HasRequests())
        {
            m_banks[i]->Print(out);
        }
    }    
}

}
