// -*- c++ -*-
#ifndef IOMESSAGEINTERFACE_H
#define IOMESSAGEINTERFACE_H

#include <arch/simtypes.h>
#include <arch/dev/IODeviceDatabase.h>
#include <arch/Interconnect.h>

namespace Simulator
{
#if 0
    typedef unsigned IODeviceID;     ///< Number of a device on an I/O Bus
    typedef unsigned IONotificationChannelID;  ///< Number of a notification/interrupt channel on an I/O bus

    static const IODeviceID INVALID_IO_DEVID = IODeviceID(-1);

/* maximum size of the data in an I/O request. */
    static const size_t MAX_IO_OPERATION_SIZE = 64;

/* the data for an I/O request. */
    struct IOData
    {
        MemSize size;
        char    data[MAX_IO_OPERATION_SIZE];
        SERIALIZE(a) {
            a & "[iod";
            a & size;
            a & Serialization::binary(data, size);
            a & "]";
        }
    };
#endif
    class IIOMessageClient
    {
    public:
        virtual bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
        virtual bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
        virtual bool OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data);
        virtual bool OnInterruptRequestReceived(IONotificationChannelID which);
        virtual bool OnNotificationReceived(IONotificationChannelID which, Integer tag);
        virtual bool OnActiveMessageReceived(IODeviceID from, MemAddr pc, Integer arg);

        virtual StorageTraceSet GetReadRequestTraces() const;
        virtual StorageTraceSet GetWriteRequestTraces() const;
        virtual StorageTraceSet GetReadResponseTraces() const;
        virtual StorageTraceSet GetInterruptRequestTraces() const;
        virtual StorageTraceSet GetNotificationTraces() const;
        virtual StorageTraceSet GetActiveMessageTraces() const;

        virtual IC::register_cb_t GetProcessRegistration();

        // Admin
        virtual void Initialize();
        virtual const std::string& GetIODeviceName() const = 0;
        virtual void GetDeviceIdentity(IODeviceIdentification& id) const = 0;

        virtual ~IIOMessageClient();

        StorageTraceSet GetRequestTraces();
    protected:
        void DefaultProcessRegistration(const Process&);
    };

    struct IOPayload
    {
        enum Type {
            READ_REQUEST,
            READ_RESPONSE,
            WRITE_REQUEST,
            INTERRUPT_REQUEST,
            NOTIFICATION,
            ACTIVE_MESSAGE
        };
        Type type;
        union {
            struct {
                IODeviceID from;
                MemAddr addr;
                MemSize size;
            } read_request;
            struct {
                IODeviceID from;
                MemAddr addr;
                IOData data;
            } write_request, read_response;
            struct {
                IONotificationChannelID channel;
                Integer tag;
            } notification;
            struct {
                IODeviceID from;
                MemAddr pc;
                Integer arg;
            } active_message;
        };
        bool Dispatch(IIOMessageClient& cl) const;
        SERIALIZE(a) { (void)a; }
    protected:
        ~IOPayload() {};
    };

    typedef IC::Message<IOPayload> IOMessage;

    class IOMessageInterface : public Object, public Inspect::Interface<Inspect::Info>
    {
        struct EndPoint {
            IIOMessageClient* m_client;
            IODeviceID m_id;
        };
        std::vector<EndPoint> m_receivers;
        std::vector<std::pair<IC::SenderKey, IC::ReceiverKey> > m_addrs;
        IC::IInterconnect<IOPayload>& m_ic;

        bool OnMessageReceived(IIOMessageClient& e, IOMessage* msg);
        StorageTraceSet GetReceiverTraces(IC::ReceiverKey rk);

    public:
        IOMessageInterface(const std::string&name, Object& parent,
                           IC::IInterconnect<IOPayload>& ic)
            : Object(name, parent),
              m_receivers(), m_addrs(), m_ic(ic) {}

        IOMessage* CreateWriteRequest(IODeviceID from, MemAddr addr, MemSize sz)
        {
            IOMessage* msg = 0;
            COMMIT {
                msg = new IOMessage;
                msg->type = IOMessage::WRITE_REQUEST;
                msg->write_request.from = from;
                msg->write_request.addr = addr;
                msg->write_request.data.size = sz;
            }
            return msg;
        }
        IOMessage* CreateReadResponse(IODeviceID from, MemAddr addr, MemSize sz)
        {
            IOMessage* msg = 0;
            COMMIT {
                msg = new IOMessage;
                msg->type = IOMessage::READ_RESPONSE;
                msg->read_response.from = from;
                msg->read_response.addr = addr;
                msg->read_response.data.size = sz;
            }
            return msg;
        }

        bool SendInterruptRequest(IODeviceID from, IONotificationChannelID to)
        {
            IOMessage* msg = 0;
            COMMIT {
                msg = new IOMessage;
                msg->type = IOMessage::INTERRUPT_REQUEST;
                msg->notification = { to, 0 };
            }
            return m_ic.SendBroadcast(GetSenderKey(from), msg);
        }

        bool SendReadRequest(IODeviceID from, IODeviceID to, MemAddr addr, MemSize sz)
        {
            IOMessage* msg = 0;
            COMMIT {
                msg = new IOMessage;
                msg->type = IOMessage::READ_REQUEST;
                msg->read_request = {from, addr, sz};
            }
            return m_ic.SendMessage(GetSenderKey(from), GetReceiverKey(to), msg);
        }
        bool SendNotification(IODeviceID from, IODeviceID to,
                              IONotificationChannelID chan, Integer arg)
        {
            IOMessage* msg = 0;
            COMMIT {
                msg = new IOMessage;
                msg->type = IOMessage::NOTIFICATION;
                msg->notification = {chan, arg};
            }
            return m_ic.SendMessage(GetSenderKey(from), GetReceiverKey(to), msg);
        }

        bool SendActiveMessage(IODeviceID from, IODeviceID to, MemAddr pc, Integer arg)
        {
            IOMessage* msg = 0;
            COMMIT {
                msg = new IOMessage;
                msg->type = IOMessage::ACTIVE_MESSAGE;
                msg->active_message = {from, pc, arg};
            }
            return m_ic.SendMessage(GetSenderKey(from), GetReceiverKey(to), msg);
        }

        bool SendMessage(IODeviceID from, IODeviceID to, IOMessage* msg) const
        { return m_ic.SendMessage(GetSenderKey(from), GetReceiverKey(to), msg); }

        Clock& RegisterClient(IODeviceID d, IIOMessageClient& client, bool rcv_ints = false);

        IC::SenderKey GetSenderKey(IODeviceID d) const { return m_addrs[d].first; }
        IC::ReceiverKey GetReceiverKey(IODeviceID d) const { return m_addrs[d].second; }
        IC::IInterconnect<IOPayload>& GetIC() const { return m_ic; }

        // Admin
        void Initialize();

        IODeviceID GetLastDeviceID() const;
        IODeviceID GetNextAvailableDeviceID() const;
        IODeviceID GetDeviceIDByName(std::string&& objname) const;
        Object& GetDeviceByName(std::string&& objname) const;
        void GetDeviceIdentity(IODeviceID which, IODeviceIdentification& id) const;

        StorageTraceSet GetRequestTraces(IODeviceID devid) const;
        StorageTraceSet GetBroadcastTraces(IODeviceID devid) const;

        /* debug */
        void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;

    };

}

#endif
