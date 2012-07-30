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
    MemSize     size;
    MemData     data;
    WClientID   wid;
    CycleNo     done;
};

class BankedMemory::Bank : public Object
{
    BankedMemory&       m_memory;
    ArbitratedService<CyclicArbitratedPort> p_incoming;
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
            const std::pair<CycleNo, CycleNo> delay = m_memory.GetMessageDelay(data ? request.size : 0);
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
            m_request.done = now + m_memory.GetMemoryDelay(request.size);
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
                if (!request.client->callback->OnMemoryWriteCompleted(request.wid)) {
                    return FAILED;
                }
            } else {
                if (!request.client->callback->OnMemoryReadCompleted(request.address, request.data.data)) {
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
                m_memory.Write(m_request.address, m_request.data.data, m_request.data.mask, m_request.size);
            } else {
                m_memory.Read(m_request.address, m_request.data.data, m_request.size);
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
            << setfill(' ') << setw(4) << dec << request.size << " | ";

        if (request.write) {
            out << "Write";
        } else { 
            out << "Read ";
        }
        out << " | " << setw(8) << dec << request.done << " |";
        if (request.write)
        {
            out << hex << setfill('0');
            for (size_t x = 0; x < request.size; ++x)
            {
                out << " ";
                if (request.data.mask[x])                    
                    out << setw(2) << (unsigned)(unsigned char)request.data.data[x];
                else
                    out << "--";
            }
        }
        else
            out << "                         ";                

        out << " | ";
    
        Object* obj = dynamic_cast<Object*>(request.client->callback);
        if (obj == NULL) {
            out << "???";
        } else {
            out << obj->GetFQN();
        }
        out << dec << endl;
    }

public:
    void RegisterClient(ArbitratedService<>& client_arbitrator, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages)
    {
        p_incoming.AddProcess(process);
        
        p_Outgoing.SetStorageTraces(storages);
        
        client_arbitrator.AddProcess(p_Outgoing);

        traces ^= m_incoming;
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
        out << "        Address       | Size | Type  |   Done   | Value(writes)            | Source" << endl;
        out << "----------------------+------+-------+----------+--------------------------+-----------------" << endl;

        for (Buffer<Request>::const_reverse_iterator p = m_incoming.rbegin(); p != m_incoming.rend(); ++p)
        {
            PrintRequest(out, '>', *p);
        }
        if (m_busy.IsSet()) {
            PrintRequest(out, '*', m_request);
        } else {
            out << "*                     |      |       |          |                          | " << endl;
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
        
        p_Incoming.SetStorageTraces(opt(m_busy));
        p_Bank.SetStorageTraces(opt(m_outgoing));
    }
};

// Time it takes for a message to traverse the memory <-> CPU network
std::pair<CycleNo, CycleNo> BankedMemory::GetMessageDelay(size_t body_size) const
{
    // Initial delay is log(N)
    // Body delay depends on size, and one cycle for the header
    return make_pair(
        (CycleNo)(log((double)m_banks.size()) / log(2.0)),
        (body_size + m_lineSize - 1) / m_lineSize + 1
    );
}

// Time it takes to process (read/write) a request once arrived
CycleNo BankedMemory::GetMemoryDelay(size_t data_size) const
{
    return m_baseRequestTime + m_timePerLine * (data_size + m_lineSize - 1) / m_lineSize;
}
                        
MCID BankedMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/)
{
#ifndef NDEBUG
    for (size_t i = 0; i < m_clients.size(); ++i) {
        assert(m_clients[i].callback != &callback);
    }
#endif
    
    MCID id = m_clients.size();

    stringstream name;
    name << "client" << id;
    ClientInfo client;
    client.service = new ArbitratedService<>(*this, m_clock, name.str());
    client.callback = &callback;
    m_clients.push_back(client);

    m_storages ^= storage;

    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        m_banks[i]->RegisterClient(*client.service, process, traces, opt(m_storages));
    }

    m_registry.registerRelation(callback.GetMemoryPeer(), *this, "mem");

    return id;
}

void BankedMemory::UnregisterClient(MCID id)
{
    assert(id < m_clients.size());
    ClientInfo& client = m_clients[id];
    assert(client.callback != NULL);
    delete client.service;
    client.callback = NULL;
}

bool BankedMemory::Read(MCID id, MemAddr address)
{
    assert(address % m_lineSize == 0);

    // Client should have been registered
    assert(id < m_clients.size() && m_clients[id].callback != NULL);

    size_t bank_index;
    MemAddr unused;
    m_selector->Map(address / m_lineSize, unused, bank_index);


    Request request;
    request.address   = address;
    request.client    = &m_clients[id];
    request.size      = m_lineSize;
    request.write     = false;
    
    Bank& bank = *m_banks[ bank_index ];
    if (!bank.AddIncomingRequest(request))
    {
        return false;
    }

    COMMIT { ++m_nreads; m_nread_bytes += m_lineSize; }
    return true;
}

bool BankedMemory::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{    
    assert(address % m_lineSize == 0);

    // Client should have been registered
    assert(id < m_clients.size() && m_clients[id].callback != NULL);

    Request request;
    request.address   = address;
    request.client    = &m_clients[id];
    request.size      = m_lineSize;
    request.wid       = wid;
    request.write     = true;
    COMMIT{
    std::copy(data.data, data.data+m_lineSize, request.data.data);
    std::copy(data.mask, data.mask+m_lineSize, request.data.mask);
    }

    // Broadcast the snoop data
    for (std::vector<ClientInfo>::iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (!p->callback->OnMemorySnooped(request.address, request.data.data, request.data.mask))
        {
            return false;
        }
    }
    
    size_t bank_index;
    MemAddr unused;
    m_selector->Map(address / m_lineSize, unused, bank_index);

    Bank& bank = *m_banks[ bank_index ];
    if (!bank.AddIncomingRequest(request))
    {
        return false;
    }

    COMMIT { ++m_nwrites; m_nwrite_bytes += m_lineSize; }
    return true;
}

void BankedMemory::Reserve(MemAddr address, MemSize size, ProcessID pid, int perm)
{
    return VirtualMemory::Reserve(address, size, pid, perm);
}

void BankedMemory::Unreserve(MemAddr address, MemSize size)
{
    return VirtualMemory::Unreserve(address, size);
}

void BankedMemory::UnreserveAll(ProcessID pid)
{
    return VirtualMemory::UnreserveAll(pid);
}

void BankedMemory::Read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::Read(address, data, size);
}

void BankedMemory::Write(MemAddr address, const void* data, const bool* mask, MemSize size)
{
    return VirtualMemory::Write(address, data, mask, size);
}

bool BankedMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    return VirtualMemory::CheckPermissions(address, size, access);
}

BankedMemory::BankedMemory(const std::string& name, Object& parent, Clock& clock, Config& config, const std::string& defaultBankSelectorType) 
    : Object(name, parent, clock),
      m_registry(config),
      m_clock(clock),
      m_banks          (config.getValueOrDefault<size_t>(*this, "NumBanks", 
                                                         config.getValue<size_t>("NumProcessors"))),
      m_baseRequestTime(config.getValue<CycleNo>(*this, "BaseRequestTime")),
      m_timePerLine    (config.getValue<CycleNo>(*this, "TimePerLine")),
      m_lineSize       (config.getValue<size_t> ("CacheLineSize")),
      m_selector       (IBankSelector::makeSelector(*this, config.getValueOrDefault<string>(*this, "BankSelector", defaultBankSelectorType), m_banks.size())),
      m_nreads         (0),
      m_nread_bytes    (0),
      m_nwrites        (0),
      m_nwrite_bytes   (0)
{
    const BufferSize buffersize = config.getValue<BufferSize>(*this, "BufferSize");
    
    config.registerObject(*this, "bmem");
    config.registerProperty(*this, "selector", m_selector->GetName());
        
    // Create the banks   
    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        stringstream name;
        name << "bank" << i;
        m_banks[i] = new Bank(name.str(), *this, clock, buffersize);

        config.registerObject(*m_banks[i], "bank");
        config.registerRelation(*this, *m_banks[i], "bank");
    }

    RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nread_bytes, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrite_bytes, SVC_CUMULATIVE);
}

BankedMemory::~BankedMemory()
{
    delete m_selector;
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
    "memory banks. Requests are sequentialized on each bank.\n"
    "This memory uses the following mapping of lines to banks: " << m_selector->GetName() <<
    "\n\nSupported operations:\n"
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
