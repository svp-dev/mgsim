#include "NullIO.h"
#include <sim/except.h>

#include <iomanip>
#include <fnmatch.h>
#include <algorithm>

using namespace std;
namespace Simulator
{


    NullIO::NullIO(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent), m_clock(clock), m_clients()
    {
    }

    void NullIO::CheckEndPoints(IODeviceID from, IODeviceID to) const
    {
        if (from >= m_clients.size() || m_clients[from] == NULL)
        {
            throw exceptf<SimulationException>(*this, "I/O from non-existent device %u", (unsigned)from);
        }

        if (to >= m_clients.size() || m_clients[to] == NULL)
        {
            throw exceptf<SimulationException>(*this, "I/O from device %u to non-existent device %u", (unsigned)from, (unsigned)to);
        }

    }

    bool NullIO::RegisterClient(IODeviceID id, IIOBusClient& client)
    {
        if (id >= m_clients.size())
        {
            m_clients.resize(id + 1, NULL);
        }
        if (m_clients[id] != NULL)
        {
            throw exceptf<InvalidArgumentException>(*this, "Device number %u is already registered", (unsigned)id);
        }
        m_clients[id] = &client;
        return true;
    }

    bool NullIO::SendReadRequest(IODeviceID from, IODeviceID to, MemAddr address, MemSize size)
    {
        CheckEndPoints(from, to);

        DebugIONetWrite("Sending read request from device %u to device %u (%#016llx/%u)", (unsigned)from, (unsigned)to, (unsigned long long)address, (unsigned)size);
        return m_clients[to]->OnReadRequestReceived(from, address, size);
    }

    bool NullIO::SendReadResponse(IODeviceID from, IODeviceID to, MemAddr address, const IOData& data)
    {
        CheckEndPoints(from, to);

        DebugIONetWrite("Sending read response from device %u to device %u (%#016llx/%u)", (unsigned)from, (unsigned)to, (unsigned long long)address, (unsigned)data.size);
        return m_clients[to]->OnReadResponseReceived(from, address, data);
    }

    bool NullIO::SendWriteRequest(IODeviceID from, IODeviceID to, MemAddr address, const IOData& data)
    {
        CheckEndPoints(from, to);

        DebugIONetWrite("Sending write request from device %u to device %u (%#016llx/%u)", (unsigned)from, (unsigned)to, (unsigned long long)address, (unsigned)data.size);
        return m_clients[to]->OnWriteRequestReceived(from, address, data);
    }

    bool NullIO::SendActiveMessage(IODeviceID from, IODeviceID to, MemAddr address, Integer arg)
    {
        CheckEndPoints(from, to);

        DebugIONetWrite("Sending active message from device %u to device %u (%#016llx/%llu)", (unsigned)from, (unsigned)to, (unsigned long long)address, (unsigned long long)arg);
        return m_clients[to]->OnActiveMessageReceived(from, address, arg);
    }

    bool NullIO::SendInterruptRequest(IODeviceID from, IONotificationChannelID which)
    {
        if (from >= m_clients.size() || m_clients[from] == NULL)
        {
            throw exceptf<SimulationException>(*this, "I/O from non-existent device %u", (unsigned)from);
        }

        DebugIONetWrite("Sending interrupt request from device %u to channel %u", (unsigned)from, (unsigned)which);

        bool res = true;
        for (auto to : m_clients)
            if (to != NULL)
                res = res & to->OnInterruptRequestReceived(which);

        return res;
    }

    bool NullIO::SendNotification(IODeviceID from, IONotificationChannelID which, Integer tag)
    {
        if (from >= m_clients.size() || m_clients[from] == NULL)
        {
            throw exceptf<SimulationException>(*this, "I/O from non-existent device %u", (unsigned)from);
        }

        DebugIONetWrite("Sending notification from device %u to channel %u (tag %#016llx)", (unsigned)from, (unsigned)which, (unsigned long long)tag);

        bool res = true;
        for (auto to : m_clients)
            if (to != NULL)
                res = res & to->OnNotificationReceived(which, tag);

        return res;
    }

    StorageTraceSet NullIO::GetReadRequestTraces(IODeviceID from) const
    {
        StorageTraceSet res;
        for (auto p : m_clients)
        {
            res ^= p->GetReadRequestTraces() * opt(m_clients[from]->GetReadResponseTraces());
        }
        return res;
    }

    StorageTraceSet NullIO::GetWriteRequestTraces() const
    {
        StorageTraceSet res;
        for (auto p : m_clients)
        {
            res ^= p->GetWriteRequestTraces();
        }
        return res;
    }

    StorageTraceSet NullIO::GetReadResponseTraces() const
    {
        StorageTraceSet res;
        for (auto p : m_clients)
        {
            res ^= p->GetReadResponseTraces();
        }
        return res;
    }

    StorageTraceSet NullIO::GetInterruptRequestTraces() const
    {
        StorageTraceSet res;
        for (auto p : m_clients)
            if (p != NULL)
                res *= p->GetInterruptRequestTraces();

        return res;
    }

    StorageTraceSet NullIO::GetNotificationTraces() const
    {
        StorageTraceSet res;
        for (auto p : m_clients)
            if (p != NULL)
                res *= p->GetNotificationTraces();

        return res;
    }

    StorageTraceSet NullIO::GetActiveMessageTraces() const
    {
        StorageTraceSet res;
        for (auto p : m_clients)
            if (p != NULL)
                res *= p->GetActiveMessageTraces();

        return res;
    }

    IODeviceID NullIO::GetNextAvailableDeviceID() const
    {
        for (size_t i = 0; i < m_clients.size(); ++i)
            if (m_clients[i] == NULL)
                return i;

        return m_clients.size();
    }

    void NullIO::Initialize()
    {
        for (auto c : m_clients)
            if (c != NULL)
                c->Initialize();

    }

    IODeviceID NullIO::GetDeviceIDByName(const std::string& name_) const
    {
        string name(name_);
        transform(name.begin(), name.end(), name.begin(), ::tolower);

        for (size_t i = 0; i < m_clients.size(); ++i)
        {
            if (m_clients[i] != NULL && FNM_NOMATCH != fnmatch(name.c_str(), m_clients[i]->GetIODeviceName().c_str(), 0))
                return i;
        }
        throw exceptf<InvalidArgumentException>(*this, "No such device: %s", name.c_str());
    }

    Object& NullIO::GetDeviceByName(const std::string& name_) const
    {
        string name(name_);
        transform(name.begin(), name.end(), name.begin(), ::tolower);

        for (auto c : m_clients)
        {
            if (c != NULL && FNM_NOMATCH != fnmatch(name.c_str(), c->GetIODeviceName().c_str(), 0))
                return dynamic_cast<Object&>(*c);
        }
        throw exceptf<InvalidArgumentException>(*this, "No such device: %s", name.c_str());
    }

    IODeviceID NullIO::GetLastDeviceID() const
    {
        return m_clients.size();
    };
    void NullIO::GetDeviceIdentity(IODeviceID which, IODeviceIdentification &id) const
    {
        if (which >= m_clients.size() || m_clients[which] == NULL)
        {
            DebugIONetWrite("I/O identification request to non-existent device %u", (unsigned)which);
            id.provider = 0;
            id.model = 0;
            id.revision = 0;
        }
        else
        {
            m_clients[which]->GetDeviceIdentity(id);
        }
    }

    void NullIO::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const
    {
        out <<
            "The Null I/O bus implements a zero-latency bus between\n"
            "the components connected to it.\n\n"
            "The following components are registered:\n";
        if (m_clients.empty())
        {
            out << "(no components registered)" << endl;
        }
        else
        {
            out <<
                "  ID  |  P  / M  / R   | Name\n"
                "------+----------------+-----------\n";
        }

        for (size_t i = 0; i < m_clients.size(); ++i)
        {
            auto c = m_clients[i];
            if (c != NULL)
            {
                IODeviceIdentification id;
                c->GetDeviceIdentity(id);

                out << setw(5) << setfill(' ') << i
                    << " | "
                    << setw(4) << setfill('0') << hex << id.provider
                    << '/'
                    << setw(4) << setfill('0') << hex << id.model
                    << '/'
                    << setw(4) << setfill('0') << hex << id.revision
                    << " | "
                    << c->GetIODeviceName()
                    << endl;
            }
        }
    }

}
