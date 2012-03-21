#include "DDRMemory.h"
#include "sim/config.h"
#include <sstream>
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

struct DDRMemory::Request
{
    ClientInfo* client;
    bool        write;
    MemAddr     address;
    MemData     data;
    TID         tid;
};

class DDRMemory::Interface : public Object, public DDRChannel::ICallback
{
    ArbitratedService<CyclicArbitratedPort> p_service;

    DDRChannel*         m_memory;    //< DDR interface
    Buffer<Request>     m_requests;  //< incoming from system, outgoing to memory
    Buffer<Request>     m_responses; //< incoming from memory, outgoing to system
    Flag                m_memready;

    Request             m_request; //< Currently active request to the DDR interface

    // Processes
    Process             p_Requests;
    Process             p_Responses;

    // Statistics
    uint64_t          m_nreads;
    uint64_t          m_nwrites;
    

public:

    // IMemory
    bool  OnReadCompleted(MemAddr address, const MemData& data)
    {
        assert(m_request.address == address);
        assert(m_request.data.size == data.size);

        COMMIT {
            memcpy(m_request.data.data, data.data, data.size);
        }

        if (!m_responses.Push(m_request))
        {
            DeadlockWrite("Unable to push reply into send buffer");
            return false;
        }
        // We're done with this request
        if (!m_memready.Set())
        {
            return false;
        }
        return true;
    }

    bool AddIncomingRequest(Request& request)
    {
        if (!p_service.Invoke())
        {
            DeadlockWrite("Unable to acquire service");
            return false;
        }

        if (!m_requests.Push(request))
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
        
        if (!m_memready.IsSet())
        {
            // We're currently processing a read that will produce a reply, stall
            return FAILED;
        }
    
        const Request& req = m_requests.Front();
        
        if (!req.write)
        {
            // It's a read
            if (!m_memory->Read(req.address, req.data.size))
            {
                return FAILED;
            }
            
            if (!m_memready.Clear())
            {
                return FAILED;
            }
            
            COMMIT{ 
                ++m_nreads;
                m_request = req;
            }
        }
        else
        {
            if (!m_memory->Write(req.address, req.data.data, req.data.size))
            {
                return FAILED;
            }

            if (!req.client->callback->OnMemoryWriteCompleted(req.tid)) {
                return FAILED;
            }

            COMMIT { 
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
        if (!request.client->service->Invoke())
        {
            return FAILED;
        }
        
        if (!request.client->callback->OnMemoryReadCompleted(request.address, request.data)) {
            return FAILED;
        }
        
        m_responses.Pop();
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
        out << " | ";
        if (request.write)
        {
            out << hex << setfill('0');
            for (size_t x = 0; x < request.data.size; ++x)
            {
                out << " ";
                out << setw(2) << (unsigned)(unsigned char)request.data.data[x];
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

    void RegisterClient(ArbitratedService<>& client_arbitrator, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages)
    {
        // XXX what is this for?
        p_service.AddProcess(process);      
        client_arbitrator.AddProcess(p_Responses);

        // XXX traces?
        // p_Requests.SetStorageTraces(storages);
        // traces ^= m_requests;
    }
    
    bool HasRequests(void) const
    {
        return !(m_requests.Empty() && m_responses.Empty() && m_memready.IsSet());
    }

    void Print(ostream& out)
    {
        out << GetName() << ":" << endl;
        out << "        Address       | Size | Type  | Value(writes)            | Source" << endl;
        out << "----------------------+------+-------+--------------------------+-----------------" << endl;

        for (Buffer<Request>::const_reverse_iterator p = m_requests.rbegin(); p != m_requests.rend(); ++p)
        {
            PrintRequest(out, '>', *p);
        }
        if (!m_memready.IsSet()) {
            PrintRequest(out, '*', m_request);
        } else {
            out << "*                     |      |       |          |                          | " << endl;
        }
        for (Buffer<Request>::const_reverse_iterator p = m_responses.rbegin(); p != m_responses.rend(); ++p)
        {
            PrintRequest(out, '<', *p);
        }
        out << endl;
    }
    
    Interface(const std::string& name, DDRMemory& parent, Clock& clock, size_t id, const DDRChannelRegistry& ddr, Config& config)
        : Object     (name, parent, clock),
          p_service  (*this, clock, "p_service"),
          m_requests ("b_requests", *this, clock, config.getValue<size_t>(*this, "ExternalOutputQueueSize")),
          m_responses("b_responses", *this, clock, config.getValue<size_t>(*this, "ExternalInputQueueSize")),
          m_memready ("f_memready", *this, clock, true),
          p_Requests (*this, "requests",   delegate::create<Interface, &Interface::DoRequests>(*this)),
          p_Responses(*this, "responses",  delegate::create<Interface, &Interface::DoResponses>(*this)),
          m_nreads(0),
          m_nwrites(0)
    {
        RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);

        config.registerObject(*this, "extif");
        config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());

        m_requests.Sensitive( p_Requests );
        m_responses.Sensitive( p_Responses );

        size_t ddrid = config.getValueOrDefault<size_t>(*this, "DDRChannelID", id);
        if (ddrid >= ddr.size())
        {
            throw exceptf<InvalidArgumentException>(*this, "Invalid DDR channel ID: %zu", ddrid);
        }
        m_memory = ddr[ddrid];
    
        StorageTraceSet sts;
        m_memory->SetClient(*this, sts, m_responses * m_memready);

        // XXX: component registry relations?
        
        // XXX: storage traces?
        // p_Requests.SetStorageTraces(m_requests);
        // p_Responses.SetStorageTraces(sts);
    }
};
                        
MCID DDRMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, Storage& storage, bool /*ignored*/)
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

    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        m_ifs[i]->RegisterClient(*client.service, process, traces, opt(m_storages));
    }

    m_registry.registerRelation(callback.GetMemoryPeer(), *this, "mem");

    // XXX: storage traces?
    // XXX: component registry relations?    

    return id;
}

void DDRMemory::UnregisterClient(MCID id)
{
    assert(id < m_clients.size());
    ClientInfo& client = m_clients[id];
    assert(client.callback != NULL);
    delete client.service;
    client.callback = NULL;
}

bool DDRMemory::Read(MCID id, MemAddr address, MemSize size)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    // Client should have been registered
    assert(id < m_clients.size() && m_clients[id].callback != NULL);

    size_t if_index;
    MemAddr unused;
    m_selector->Map(address / m_lineSize, unused, if_index);

    Request request;
    request.address   = address;
    request.client    = &m_clients[id];
    request.data.size = size;
    request.write     = false;
    
    Interface& chan = *m_ifs[ if_index ];
    if (!chan.AddIncomingRequest(request))
    {
        return false;
    }

    COMMIT { ++m_nreads; m_nread_bytes += size; }
    return true;
}

bool DDRMemory::Write(MCID id, MemAddr address, const void* data, MemSize size, TID tid)
{
    if (size > MAX_MEMORY_OPERATION_SIZE)
    {
        throw InvalidArgumentException("Size argument too big");
    }
    
    // Client should have been registered
    assert(id < m_clients.size() && m_clients[id].callback != NULL);

    Request request;
    request.address   = address;
    request.client    = &m_clients[id];
    request.data.size = size;
    request.tid       = tid;
    request.write     = true;
    memcpy(request.data.data, data, (size_t)size);

    // Broadcast the snoop data
    for (std::vector<ClientInfo>::iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (!p->callback->OnMemorySnooped(address, request.data))
        {
            return false;
        }
    }
    
    size_t if_index;
    MemAddr unused;
    m_selector->Map(address / m_lineSize, unused, if_index);

    Interface& chan = *m_ifs[ if_index ];
    if (!chan.AddIncomingRequest(request))
    {
       return false;
    }

    COMMIT { ++m_nwrites; m_nwrite_bytes += size; }
    return true;
}

void DDRMemory::Reserve(MemAddr address, MemSize size, int perm)
{
    return VirtualMemory::Reserve(address, size, perm);
}

void DDRMemory::Unreserve(MemAddr address)
{
    return VirtualMemory::Unreserve(address);
}

bool DDRMemory::Allocate(MemSize size, int perm, MemAddr& address)
{
    return VirtualMemory::Allocate(size, perm, address);
}

void DDRMemory::Read(MemAddr address, void* data, MemSize size)
{
    return VirtualMemory::Read(address, data, size);
}

void DDRMemory::Write(MemAddr address, const void* data, MemSize size)
{
    return VirtualMemory::Write(address, data, size);
}

bool DDRMemory::CheckPermissions(MemAddr address, MemSize size, int access) const
{
    return VirtualMemory::CheckPermissions(address, size, access);
}

DDRMemory::DDRMemory(const std::string& name, Object& parent, Clock& clock, Config& config, const std::string& defaultInterfaceSelectorType) 
    : Object(name, parent, clock),
      m_registry(config),
      m_clock(clock),
      m_ifs       (config.getValueOrDefault<size_t>(*this, "NumInterfaces", 
                                                         config.getValue<size_t>("NumProcessors"))),
      m_ddr            ("ddr", *this, *this, config, m_ifs.size()),
      m_lineSize       (config.getValue<size_t> ("CacheLineSize")),
      m_selector       (IBankSelector::makeSelector(*this, config.getValueOrDefault<string>(*this, "InterfaceSelector", defaultInterfaceSelectorType), m_ifs.size())),
      m_nreads         (0),
      m_nread_bytes    (0),
      m_nwrites        (0),
      m_nwrite_bytes   (0)
{
    config.registerObject(*this, "ddrmem");
    config.registerProperty(*this, "selector", m_selector->GetName());
        
    // Create the interfaces
    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        stringstream name;
        name << "extif" << i;

        m_ifs[i] = new Interface(name.str(), *this, clock, i, m_ddr, config);

        config.registerObject(*m_ifs[i], "extif");
        config.registerRelation(*this, *m_ifs[i], "extif");
    }

    RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nread_bytes, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_nwrite_bytes, SVC_CUMULATIVE);
}

DDRMemory::~DDRMemory()
{
    delete m_selector;
    for (size_t i = 0; i < m_ifs.size(); ++i)
    {
        delete m_ifs[i];
    }
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
