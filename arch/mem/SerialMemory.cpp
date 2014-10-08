#include "arch/mem/SerialMemory.h"
#include "sim/config.h"

#include <cassert>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

MCID SerialMemory::RegisterClient(IMemoryCallback& callback, Process& process, StorageTraceSet& traces, const StorageTraceSet& storages, bool /*ignored*/)
{
    assert(std::find(m_clients.begin(), m_clients.end(), &callback) == m_clients.end());
    m_clients.push_back(&callback);

    p_requests.AddProcess(process);
    traces = m_requests;

    m_storages = m_storages ^ storages;
    p_Requests.SetStorageTraces(m_storages);

    RegisterModelRelation(callback.GetMemoryPeer(), *this, "mem");

    return m_clients.size() - 1;
}

void SerialMemory::UnregisterClient(MCID id)
{
    assert(id < m_clients.size() && m_clients[id] != NULL);
    m_clients[id] = NULL;
}

bool SerialMemory::Read(MCID id, MemAddr address)
{
    assert(address % m_lineSize == 0);

    if (!p_requests.Invoke())
    {
        return false;
    }

    // Client should have registered
    assert(id < m_clients.size() && m_clients[id] != NULL);

    Request request;
    request.client    = id;
    request.address   = address;
    request.write     = false;

    if (!m_requests.Push(request))
    {
        return false;
    }

    return true;
}

bool SerialMemory::Write(MCID id, MemAddr address, const MemData& data, WClientID wid)
{
    assert(address % m_lineSize == 0);

    if (!p_requests.Invoke())
    {
        return false;
    }

    // Client should have registered
    assert(id < m_clients.size() && m_clients[id] != NULL);

    Request request;
    request.client    = id;
    request.address   = address;
    request.wid       = wid;
    request.write     = true;
    COMMIT{
    std::copy(data.data, data.data + m_lineSize, request.data.data);
    std::copy(data.mask, data.mask + m_lineSize, request.data.mask);
    }

    if (!m_requests.Push(request))
    {
        return false;
    }

    // Broadcast the snoop data
    for (vector<IMemoryCallback*>::const_iterator p = m_clients.begin(); p != m_clients.end(); ++p)
    {
        if (*p != NULL && !(*p)->OnMemorySnooped(request.address, request.data.data, request.data.mask))
        {
            return false;
        }
    }

    return true;
}

Result SerialMemory::DoRequests()
{
    assert(!m_requests.Empty());

    const Request& request = m_requests.Front();
    const CycleNo  now     = GetKernel()->GetActiveClock()->GetCycleNo();

    if (m_nextdone > 0)
    {
        // There is already a request active
        if (now >= m_nextdone)
        {
            // The current request has completed
            if (request.write) {

                VirtualMemory::Write(request.address, request.data.data, request.data.mask, m_lineSize);

                if (!m_clients[request.client]->OnMemoryWriteCompleted(request.wid))
                {
                    return FAILED;
                }
                COMMIT {
                    ++m_nwrites;
                    m_nwrite_bytes += m_lineSize;
                }
            } else {
                char data[m_lineSize];

                VirtualMemory::Read(request.address, data, m_lineSize);

                if (!m_clients[request.client]->OnMemoryReadCompleted(request.address, data))
                {
                    return FAILED;
                }
                COMMIT {
                    ++m_nreads;
                    m_nread_bytes += m_lineSize;
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
            CycleNo requestTime = m_baseRequestTime + m_timePerLine;
            m_nextdone = now + requestTime;
        }
    }
    return SUCCESS;
}

SerialMemory::SerialMemory(const std::string& name, Object& parent, Clock& clock) :
    Object(name, parent),
    m_clients        (),
    InitBuffer(m_requests, clock, "BufferSize"),
    p_requests       (clock, GetName() + ".m_requests"),
    m_baseRequestTime(GetConf("BaseRequestTime", CycleNo)),
    m_timePerLine    (GetConf("TimePerLine", CycleNo)),
    m_lineSize       (GetTopConf("CacheLineSize", CycleNo)),
    InitStateVariable(nextdone, 0),
    m_storages(),
    InitSampleVariable(nreads, SVC_CUMULATIVE),
    InitSampleVariable(nread_bytes, SVC_CUMULATIVE),
    InitSampleVariable(nwrites, SVC_CUMULATIVE),
    InitSampleVariable(nwrite_bytes, SVC_CUMULATIVE),

    InitProcess(p_Requests, DoRequests)
{
    m_requests.Sensitive( p_Requests );
    RegisterModelObject(*this, "sermem");

    m_storages = StorageTraceSet(StorageTrace());   // Request handler is waiting for completion
    p_Requests.SetStorageTraces(m_storages);

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

    out << "      Address       | Type  | Source               | Value(writes)" << endl;
    out << "--------------------+-------+----------------------+----------------" << endl;

    for (Buffer<Request>::const_iterator p = m_requests.begin(); p != m_requests.end(); ++p)
    {
        out << hex << setfill('0') << right
            << " 0x" << setw(16) << p->address << " | ";

        if (p->write) {
            out << "Write";
        } else {
            out << "Read ";
        }
        out << " | "
            << setw(20);

        Object* obj = dynamic_cast<Object*>(m_clients[p->client]);
        if (obj == NULL) {
            out << "???";
        } else {
            out << obj->GetName();
        }

        out << " |"
            << hex << setfill('0');
        if (p->write)
        {
            for (size_t i = 0; i < m_lineSize; ++i)
            {
                out << ' ';
                if (p->data.mask[i])
                    out << setw(2) << (unsigned)(unsigned char)p->data.data[i];
                else
                    out << "--";
            }
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
