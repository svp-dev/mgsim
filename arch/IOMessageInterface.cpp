#include <arch/IOMessageInterface.h>
#include <sim/delegate_closure.h>
#include <algorithm>
#include <iomanip>
#include <fnmatch.h>

using namespace std;

namespace Simulator
{
    bool IOPayload::Dispatch(IIOMessageClient& cl) const
    {
        switch(type)
        {
        case IOPayload::READ_REQUEST:
            return cl.OnReadRequestReceived(read_request.from,
                                            read_request.addr, read_request.size);
        case IOPayload::READ_RESPONSE:
            return cl.OnReadResponseReceived(read_response.from,
                                             read_response.addr, read_response.data);
        case IOPayload::WRITE_REQUEST:
            return cl.OnWriteRequestReceived(write_request.from,
                                             write_request.addr, write_request.data);
        case IOPayload::INTERRUPT_REQUEST:
            return cl.OnInterruptRequestReceived(notification.channel);
        case IOPayload::NOTIFICATION:
            return cl.OnNotificationReceived(notification.channel, notification.tag);
        case IOPayload::ACTIVE_MESSAGE:
            return cl.OnActiveMessageReceived(active_message.from,
                                              active_message.pc, active_message.arg);
        }
        return false;
    }

    bool IOMessageInterface::OnMessageReceived(IIOMessageClient &client, IOMessage* msg)
    {
        bool res = msg->Dispatch(client);
        COMMIT {
            if (res)
                delete msg;
        }
        return res;
    }


    IIOMessageClient::~IIOMessageClient()
    { }

    StorageTraceSet IIOMessageClient::GetReadRequestTraces() const { return StorageTraceSet(); }
    StorageTraceSet IIOMessageClient::GetWriteRequestTraces() const { return StorageTraceSet(); }
    StorageTraceSet IIOMessageClient::GetReadResponseTraces() const { return StorageTraceSet(); }
    StorageTraceSet IIOMessageClient::GetInterruptRequestTraces() const { return StorageTraceSet(); }
    StorageTraceSet IIOMessageClient::GetNotificationTraces() const { return StorageTraceSet(); }
    StorageTraceSet IIOMessageClient::GetActiveMessageTraces() const { return StorageTraceSet(); }

    StorageTraceSet IIOMessageClient::GetRequestTraces()
    {
        return GetReadRequestTraces()
            ^ GetWriteRequestTraces()
            ^ GetReadResponseTraces()
            ^ GetInterruptRequestTraces()
            ^ GetNotificationTraces()
            ^ GetActiveMessageTraces();
    }

    IC::register_cb_t IIOMessageClient::GetProcessRegistration()
    {
        return IC::create_register_cb<IIOMessageClient, &IIOMessageClient::DefaultProcessRegistration>(*this);
    }

    void IIOMessageClient::DefaultProcessRegistration(const Process&)
    {}

    IIOBus::~IIOBus()
    { }

    void IIOMessageClient::Initialize()
    {}

    void IOMessageInterface::Initialize()
    {
        for (auto &r : m_receivers)
            r.m_client->Initialize();
    }

    bool IIOMessageClient::OnReadRequestReceived(IODeviceID from, MemAddr /*address*/, MemSize /*size*/)
    {
        throw exceptf<>("Unsupported read request received from device %u", (unsigned)from);
    }

    bool IIOMessageClient::OnWriteRequestReceived(IODeviceID from, MemAddr /*address*/, const IOData& /*data*/)
    {
        throw exceptf<>("Unsupported write request received from device %u", (unsigned)from);
    }

    bool IIOMessageClient::OnActiveMessageReceived(IODeviceID from, MemAddr /*address*/, Integer /*arg*/)
    {
        throw exceptf<>("Unsupported active message received from device %u", (unsigned)from);
    }

    bool IIOMessageClient::OnReadResponseReceived(IODeviceID from, MemAddr /*address*/, const IOData& /*data*/)
    {
        throw exceptf<>("Unsupported read response received from device %u", (unsigned)from);
    }

    bool IIOMessageClient::OnInterruptRequestReceived(IONotificationChannelID /*which*/)
    {
        return true;
    }

    bool IIOMessageClient::OnNotificationReceived(IONotificationChannelID /*which*/, Integer /*tag*/)
    {
        return true;
    }

    Clock& IOMessageInterface::RegisterClient(IODeviceID id, IIOMessageClient& client, bool rcv_ints)
    {
        if (id >= m_addrs.size())
        {
            m_addrs.resize(id + 1, std::make_pair(IC::INVALID_KEY, IC::INVALID_KEY));
        }
        if (m_addrs[id].first != IC::INVALID_KEY)
        {
            throw exceptf<InvalidArgumentException>(*this, "Device number %u is already registered", (unsigned)id);
        }
        auto rk = m_ic.RegisterReceiver(client.GetIODeviceName());
        auto sk = m_ic.RegisterSender(client.GetIODeviceName());

        m_addrs[id] = std::make_pair(sk, rk);

        if (rk >= m_receivers.size())
        {
            m_receivers.resize(rk + 1);
        }

        auto &e = m_receivers[rk];
        e.m_client = &client;
        e.m_id = id;

        m_ic.ConnectReceiver(rk,
                             closure<bool, IOMessage*>
                             ::adapter<bool, IOMessage*>
                             ::capture<IIOMessageClient&>
                             ::create<IOMessageInterface, &IOMessageInterface::OnMessageReceived>(*this, client),
                             client.GetProcessRegistration(),
                             IC::traces_cb_t::create<IIOMessageClient, &IIOMessageClient::GetRequestTraces>(client),
                             rcv_ints);

        return m_ic.GetSenderClock(sk);
    }

    StorageTraceSet IOMessageInterface::GetRequestTraces(IODeviceID devid) const
    {
        return m_ic.GetRequestTraces(GetSenderKey(devid));
    }

    StorageTraceSet IOMessageInterface::GetBroadcastTraces(IODeviceID devid) const
    {
        return m_ic.GetBroadcastTraces(GetSenderKey(devid));
    }

    IODeviceID IOMessageInterface::GetNextAvailableDeviceID() const
    {
        for (IODeviceID i = 0; i < m_addrs.size(); ++i)
            if (m_addrs[i].first == IC::INVALID_KEY)
                return i;
        return m_addrs.size();
    }

    IODeviceID IOMessageInterface::GetLastDeviceID() const
    {
        return m_addrs.size();
    };

    IODeviceID IOMessageInterface::GetDeviceIDByName(std::string&& name_) const
    {
        string name(name_);
        transform(name.begin(), name.end(), name.begin(), ::tolower);

        for (size_t i = 0; i < m_receivers.size(); ++i)
        {
            auto cl = m_receivers[i].m_client;
            if (cl != NULL && FNM_NOMATCH != fnmatch(name.c_str(), cl->GetIODeviceName().c_str(), 0))
                return m_receivers[i].m_id;
        }
        throw exceptf<InvalidArgumentException>(*this, "No such device: %s", name.c_str());
    }

    Object& IOMessageInterface::GetDeviceByName(std::string&& name_) const
    {
        string name(name_);
        transform(name.begin(), name.end(), name.begin(), ::tolower);

        for (auto &e : m_receivers)
        {
            auto c = e.m_client;
            if (c != NULL && FNM_NOMATCH != fnmatch(name.c_str(), c->GetIODeviceName().c_str(), 0))
                return dynamic_cast<Object&>(*c);
        }
        throw exceptf<InvalidArgumentException>(*this, "No such device: %s", name.c_str());
    }

    void IOMessageInterface::GetDeviceIdentity(IODeviceID which, IODeviceIdentification &id) const
    {
        if (which >= m_addrs.size() || m_addrs[which].first == IC::INVALID_KEY)
        {
            DebugIONetWrite("I/O identification request to non-existent device %u", (unsigned)which);
            id.provider = 0;
            id.model = 0;
            id.revision = 0;
        }
        else
        {
            m_receivers[m_addrs[which].second].m_client->GetDeviceIdentity(id);
        }
    }

    void IOMessageInterface::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const
    {
        out << "The following components are registered:" << endl;
        if (m_receivers.empty())
        {
            out << "(no components registered)" << endl;
        }
        else
        {
            out <<
                "  ID  |  P  / M  / R   | Name\n"
                "------+----------------+-----------\n";
        }

        for (auto& r : m_receivers)
        {
            auto c = r.m_client;
            IODeviceIdentification id;
            c->GetDeviceIdentity(id);

            out << setw(5) << setfill(' ') << r.m_id
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
