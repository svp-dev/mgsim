#include <arch/mem/DDRMemory.h>
#include <sim/config.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

struct DDRMemory::ClientInfo
{
    IMemoryCallback*     callback;
    ArbitratedService<>* service;
};

class DDRMemory::Interface : public Object, public DDRChannel::ICallback
{
    const vector<ClientInfo>& m_clients;
    size_t              m_lineSize;  //< Cache line size

    ArbitratedService<CyclicArbitratedPort> p_service;

    DDRChannel*         m_ddr;       //< DDR interface
    StorageTraceSet     m_ddrStorageTraces;
    Buffer<Request>     m_requests;  //< incoming from system, outgoing to memory
    Buffer<Request>     m_responses; //< incoming from memory, outgoing to system

    std::queue<Request> m_activeRequests; //< Requests currently active in DDR

    // Processes
    Process             p_Requests;
    Process             p_Responses;

    VirtualMemory&      m_memory;

    // Statistics
    DefineSampleVariable(uint64_t, nreads);
    DefineSampleVariable(uint64_t, nwrites);


public:

    // IMemory
    bool OnReadCompleted()
    {
        assert(!m_activeRequests.empty());

        Request& request = m_activeRequests.front();

        COMMIT {
            m_memory.Read(request.address, request.data.data, m_lineSize);
        }

        if (!m_responses.Push(std::move(request)))
        {
            DeadlockWrite("Unable to push reply into send buffer");
            return false;
        }

        COMMIT {
            m_activeRequests.pop();
        }

        return true;
    }

    bool AddIncomingRequest(Request&& request)
    {
        if (!p_service.Invoke())
        {
            DeadlockWrite("Unable to acquire service");
            return false;
        }

        if (!m_requests.Push(std::move(request)))
        {
            DeadlockWrite("Unable to queue read request to memory");
            return false;
        }

        return true;
    }

    Result DoRequests()
    {
        // Handle requests from memory clients to DDR,
        // queued by AddRequest()

        assert(!m_requests.Empty());

        const Request& req = m_requests.Front();

        if (!req.write)
        {
            // It's a read
            if (!m_ddr->Read(req.address, m_lineSize))
            {
                return FAILED;
            }

            COMMIT{
                ++m_nreads;
                m_activeRequests.push(req);
            }
        }
        else
        {
            if (!m_ddr->Write(req.address, m_lineSize))
            {
                return FAILED;
            }

            if (!m_clients[req.client].callback->OnMemoryWriteCompleted(req.wid)) {
                return FAILED;
            }

            COMMIT {
                m_memory.Write(req.address, req.data.data, req.data.mask, m_lineSize);

                ++m_nwrites;
            }
        }

        m_requests.Pop();
        return SUCCESS;
    }

    Result DoResponses()
    {
        // Handle read responses from DDR towards clients,
        // produced by OnReadCompleted

        assert(!m_responses.Empty());
        const Request &request = m_responses.Front();

        assert(!request.write);

        // This request has arrived, send it to the callback
        if (!m_clients[request.client].service->Invoke())
        {
            return FAILED;
        }

        if (!m_clients[request.client].callback->OnMemoryReadCompleted(request.address, request.data.data)) {
            return FAILED;
        }

        m_responses.Pop();
        return SUCCESS;
    }

    void PrintRequest(ostream& out, char prefix, const Request& request, size_t lineSize)
    {
        out << prefix << " "
            << hex << setfill('0') << right
            << " 0x" << setw(16) << request.address << " | ";

        if (request.write) {
            out << "Write";
        } else {
            out << "Read ";
        }
        out << " | ";
        if (request.write)
        {
            out << hex << setfill('0');
            for (size_t x = 0; x < lineSize; ++x)
            {
                if (request.data.mask[x])
                    out << " " << setw(2) << (unsigned)(unsigned char)request.data.data[x];
                else
                    out << " --";
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

    void RegisterClient(ArbitratedService<>& client_arbitrator, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages)
    {
        p_service.AddProcess(process);
        client_arbitrator.AddProcess(p_Responses);

        p_Requests.SetStorageTraces(m_ddrStorageTraces * storages);
        p_Responses.SetStorageTraces(storages);
        traces ^= m_requests;
    }

    bool HasRequests(void) const
    {
        return !(m_requests.Empty() && m_responses.Empty());
    }

    void Print(ostream& out)
    {
        out << GetName() << ":" << endl;
        out << "        Address       | Type  | Value(writes)            | Source" << endl;
        out << "----------------------+-------+--------------------------+-----------------" << endl;

        for (Buffer<Request>::const_reverse_iterator p = m_requests.rbegin(); p != m_requests.rend(); ++p)
        {
            PrintRequest(out, '>', *p, m_lineSize);
        }
        out << "*                     |       |          |                          | " << endl;
        for (Buffer<Request>::const_reverse_iterator p = m_responses.rbegin(); p != m_responses.rend(); ++p)
        {
            PrintRequest(out, '<', *p, m_lineSize);
        }
        out << endl;
    }

    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;

    Interface(const std::string& name, DDRMemory& parent, Clock& clock, size_t id, const DDRChannelRegistry& ddr, const vector<ClientInfo>& clients)
        : Object     (name, parent),
          m_clients  (clients),
          m_lineSize (GetTopConf("CacheLineSize", size_t)),
          p_service  (clock, GetName() + ".p_service"),
          m_ddr      (0),
          m_ddrStorageTraces(),
          InitStorage(m_requests, clock, GetConf("ExternalOutputQueueSize", size_t)),
          InitStorage(m_responses, clock, GetConf("ExternalInputQueueSize", size_t)),
          m_activeRequests(),

          InitProcess(p_Requests, DoRequests),
          InitProcess(p_Responses, DoResponses),
          m_memory(parent),
          InitSampleVariable(nreads, SVC_CUMULATIVE),
          InitSampleVariable(nwrites, SVC_CUMULATIVE)
    {

        RegisterModelObject(*this, "extif");
        RegisterModelProperty(*this, "freq", (uint32_t)clock.GetFrequency());

        m_requests.Sensitive( p_Requests );
        m_responses.Sensitive( p_Responses );

        size_t ddrid = GetConfOpt("DDRChannelID", size_t, id);
        if (ddrid >= ddr.size())
        {
            throw exceptf<InvalidArgumentException>(*this, "Invalid DDR channel ID: %zu", ddrid);
        }
        m_ddr = ddr[ddrid];

        m_ddr->SetClient(*this, m_ddrStorageTraces, m_responses);

        //p_Requests.SetStorageTraces(m_requests);
        //p_Responses.SetStorageTraces(sts);
    }
};

MCID DDRMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool /*ignored*/)
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

    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        m_ifs[i]->RegisterClient(*client.service, process, traces, opt(m_storages));
    }

    RegisterModelRelation(callback.GetMemoryPeer(), *this, "mem");

    return id;
}

void DDRMemory::UnregisterClient(MCID id)
{
    assert(id < m_clients.size());
    ClientInfo& client = m_clients[id];
    assert(client.callback != NULL);
    delete client.service;
    client.service = NULL;
    client.callback = NULL;
}

bool DDRMemory::Read(MCID id, MemAddr address)
{
    assert(address % m_lineSize == 0);

    // Client should have been registered
    assert(id < m_clients.size() && m_clients[id].callback != NULL);

    size_t if_index;
    MemAddr unused;
    m_selector->Map(address, unused, if_index);

    Request request;
    request.address   = address;
    request.client    = id;
    request.write     = false;

    Interface& chan = *m_ifs[ if_index ];
    if (!chan.AddIncomingRequest(std::move(request)))
    {
        return false;
    }

    COMMIT { ++m_nreads; m_nread_bytes += m_lineSize; }
    return true;
}

bool DDRMemory::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
    assert(address % m_lineSize == 0);

    // Client should have been registered
    assert(id < m_clients.size() && m_clients[id].callback != NULL);

    Request request;
    request.address   = address;
    request.client    = id;
    request.wid       = wid;
    request.write     = true;
    COMMIT{
    std::copy(data.data, data.data + m_lineSize, request.data.data);
    std::copy(data.mask, data.mask + m_lineSize, request.data.mask);
    }

    // Broadcast the snoop data
    for (std::vector<ClientInfo>::iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (!p->callback->OnMemorySnooped(address, request.data.data, request.data.mask))
        {
            return false;
        }
    }

    size_t if_index;
    MemAddr unused;
    m_selector->Map(address / m_lineSize, unused, if_index);

    Interface& chan = *m_ifs[ if_index ];
    if (!chan.AddIncomingRequest(std::move(request)))
    {
       return false;
    }

    COMMIT { ++m_nwrites; m_nwrite_bytes += m_lineSize; }
    return true;
}

DDRMemory::DDRMemory(const std::string& name, Object& parent, Clock& clock, const std::string& defaultInterfaceSelectorType)
    : VirtualMemory(name, parent),
      m_clock(clock),
      m_clients(),
      m_storages(),
      m_ifs            (GetConf("NumInterfaces", size_t)),
      m_ddr            ("ddr", *this, m_ifs.size()),
      m_lineSize       (GetTopConf("CacheLineSize", size_t)),
      m_selector       (IBankSelector::makeSelector(*this, GetConfOpt("InterfaceSelector", string, defaultInterfaceSelectorType), m_ifs.size())),
      InitSampleVariable(nreads, SVC_CUMULATIVE),
      InitSampleVariable(nread_bytes, SVC_CUMULATIVE),
      InitSampleVariable(nwrites, SVC_CUMULATIVE),
      InitSampleVariable(nwrite_bytes, SVC_CUMULATIVE)
{
    RegisterModelObject(*this, "ddrmem");
    RegisterModelProperty(*this, "selector", m_selector->GetName());

    // Create the interfaces
    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        m_ifs[i] = new Interface("extif" + to_string(i), *this, clock, i, m_ddr, m_clients);

        RegisterModelObject(*m_ifs[i], "extif");
        RegisterModelRelation(*this, *m_ifs[i], "extif");
    }

}

DDRMemory::~DDRMemory()
{
    delete m_selector;
    for (auto i : m_ifs)
        delete i;
    for (auto& c : m_clients)
        if (c.service)
            delete c.service;
}

void DDRMemory::Cmd_Info(ostream& out, const vector<string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "ranges")
    {
        return VirtualMemory::Cmd_Info(out, arguments);
    }
    out <<
    "The DDR Memory represents a switched memory network between P processors and N\n"
    "interfaces to 1 DDR channel each.\n"
    "This memory uses the following mapping of lines to interfaces: " << m_selector->GetName() <<
    "\n\nSupported operations:\n"
    "- info <component> ranges\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- inspect <component> requests\n"
    "  Reads the channel requests buffers and queues\n"
    "- inspect <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n";
}

void DDRMemory::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    if (arguments.empty() || arguments[0] != "requests")
    {
        return VirtualMemory::Cmd_Read(out, arguments);
    }

    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        if (m_ifs[i]->HasRequests())
        {
            m_ifs[i]->Print(out);
        }
    }
}

}
