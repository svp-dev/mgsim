#include <arch/mem/BankedMemory.h>
#include <sim/config.h>
#include <sim/flag.h>
#include <sim/buffer.h>
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

class BankedMemory::Bank : public Object
{
    const vector<ClientInfo>& m_clients;
    BankedMemory&       m_memory;
    ArbitratedService<CyclicArbitratedPort> p_incoming;
    Buffer<Request>     m_incoming;
    Buffer<Request>     m_outgoing;
    Flag                m_busy;
    DefineStateVariable(Request, request);
    Process             p_Incoming;
    Process             p_Outgoing;
    Process             p_Bank;

    bool AddRequest(Buffer<Request>& queue, Request& request, bool data)
    {
        COMMIT
        {
            const std::pair<CycleNo, CycleNo> delay = m_memory.GetMessageDelay(data ? request.size : 0);
            const CycleNo                     now   = GetKernel()->GetActiveClock()->GetCycleNo();

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

        const CycleNo  now     = GetKernel()->GetActiveClock()->GetCycleNo();
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

        const CycleNo  now     = GetKernel()->GetActiveClock()->GetCycleNo();
        const Request& request = m_outgoing.Front();
        if (now >= request.done)
        {
            // This request has arrived, send it to the callback
            if (!m_clients[request.client].service->Invoke())
            {
                return FAILED;
            }

            if (request.write) {
                if (!m_clients[request.client].callback->OnMemoryWriteCompleted(request.wid)) {
                    return FAILED;
                }
            } else {
                if (!m_clients[request.client].callback->OnMemoryReadCompleted(request.address, request.data.data)) {
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
        if (GetKernel()->GetActiveClock()->GetCycleNo() >= m_request.done)
        {
            // This bank is done serving the request
            if (m_request.write) {
                static_cast<VirtualMemory&>(m_memory).Write(m_request.address, m_request.data.data, m_request.data.mask, m_request.size);
            } else {
                static_cast<VirtualMemory&>(m_memory).Read(m_request.address, m_request.data.data, m_request.size);
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

    void PrintRequest(ostream& out, char prefix, const Request& request)
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

        Object* obj = dynamic_cast<Object*>(m_clients[request.client].callback);
        if (obj == NULL) {
            out << "???";
        } else {
            out << obj->GetName();
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

    Bank(const std::string& name, BankedMemory& memory, Clock& clock, BufferSize buffersize, const vector<ClientInfo>& clients)
        : Object(name, memory),
          m_clients(clients),
          m_memory  (memory),
          p_incoming(clock, name + ".incoming"),
          InitStorage(m_incoming, clock, buffersize),
          InitStorage(m_outgoing, clock, buffersize),
          InitStorage(m_busy, clock, false),
          m_request(),
          InitProcess(p_Incoming, DoIncoming),
          InitProcess(p_Outgoing, DoOutgoing),
          InitProcess(p_Bank, DoRequest)
    {
        RegisterStateVariable(m_request.client, "request.client");
        RegisterStateVariable(m_request.write, "request.write");
        RegisterStateVariable(m_request.address, "request.address");
        RegisterStateVariable(m_request.size, "request.size");
        RegisterStateArray(m_request.data.data, sizeof(m_request.data.data)/sizeof(m_request.data.data[0]), "request.data");
        RegisterStateArray(m_request.data.mask, sizeof(m_request.data.mask)/sizeof(m_request.data.mask[0]), "request.mask");
        RegisterStateVariable(m_request.wid, "request.wid");
        RegisterStateVariable(m_request.done, "request.done");

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

MCID BankedMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool /*ignored*/)
{
#ifndef NDEBUG
    for (size_t i = 0; i < m_clients.size(); ++i) {
        assert(m_clients[i].callback != &callback);
    }
#endif

    MCID id = m_clients.size();

    ClientInfo client;
    client.service = new ArbitratedService<>(m_clock, GetName() + ".client" + to_string(id));
    client.callback = &callback;
    m_clients.push_back(client);

    m_storages ^= storages;

    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        m_banks[i]->RegisterClient(*client.service, process, traces, opt(m_storages));
    }

    RegisterModelRelation(callback.GetMemoryPeer(), *this, "mem");

    return id;
}

void BankedMemory::UnregisterClient(MCID id)
{
    assert(id < m_clients.size());
    ClientInfo& client = m_clients[id];
    assert(client.callback != NULL);
    delete client.service;
    client.service = NULL;
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
    request.client    = id;
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
    request.client    = id;
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

BankedMemory::BankedMemory(const std::string& name, Object& parent, Clock& clock, const std::string& defaultBankSelectorType)
    : VirtualMemory(name, parent),
      m_clock(clock),
      m_clients(),
      m_storages(),
      m_banks          (GetConf("NumBanks", size_t)),
      m_baseRequestTime(GetConf("BaseRequestTime", CycleNo)),
      m_timePerLine    (GetConf("TimePerLine", CycleNo)),
      m_lineSize       (GetTopConf("CacheLineSize", size_t)),
      m_selector       (IBankSelector::makeSelector(*this, GetConfOpt("BankSelector", string, defaultBankSelectorType), m_banks.size())),
      InitSampleVariable(nreads, SVC_CUMULATIVE),
      InitSampleVariable(nread_bytes, SVC_CUMULATIVE),
      InitSampleVariable(nwrites, SVC_CUMULATIVE),
      InitSampleVariable(nwrite_bytes, SVC_CUMULATIVE)
{
    const BufferSize buffersize = GetConf("BufferSize", BufferSize);

    RegisterModelObject(*this, "bmem");
    RegisterModelProperty(*this, "selector", m_selector->GetName());

    // Create the banks
    for (size_t i = 0; i < m_banks.size(); ++i)
    {
        m_banks[i] = new Bank("bank" + to_string(i), *this, clock, buffersize, m_clients);

        RegisterModelObject(*m_banks[i], "bank");
        RegisterModelRelation(*this, *m_banks[i], "bank");
    }

}

BankedMemory::~BankedMemory()
{
    delete m_selector;
    for (auto b : m_banks)
        delete b;
    for (auto& c : m_clients)
        if (c.service)
            delete c.service;
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
