// -*- c++ -*-
#ifndef INTERCONNECT_H
#define INTERCONNECT_H

#include <arch/IOBus.h>
#include <sim/kernel.h>
#include <sim/inspect.h>
#include <sim/delegate.h>

#include <vector>

namespace Simulator
{
    namespace IC {
        typedef size_t SenderKey;
        typedef size_t ReceiverKey;
        constexpr size_t INVALID_KEY = (size_t)-1;

        template<typename Payload>
        class Message;


        typedef delegate_gen<void, const Process&> register_cb_t;
        typedef delegate_gen<StorageTraceSet> traces_cb_t;

        template<typename Payload>
        using  receiver_cb_t = delegate_gen<bool, Message<Payload>*>;

        /// IInterconnect: what the clients see.
        template<typename Payload>
        class IInterconnect
        {
        public:
            typedef Payload PayloadType;
            typedef Message<Payload> MessageType;

            virtual bool SendMessage(SenderKey key, ReceiverKey dst, MessageType* msg) = 0;
            virtual bool SendBroadcast(SenderKey key, MessageType* msg) = 0;

            //virtual bool IsBroadcastReceiver(ReceiverKey dst) const = 0;

            virtual ReceiverKey RegisterReceiver(const std::string& lname) = 0;
            virtual SenderKey RegisterSender(const std::string& lname) = 0;

            virtual Clock& GetSenderClock(SenderKey sk) const = 0;

            virtual void ConnectReceiver(ReceiverKey rk,
                                         receiver_cb_t<Payload>&& deliver_cb,
                                         register_cb_t&& register_cb,
                                         traces_cb_t&& req_traces_cb,
                                         bool bcast_enabled = true) = 0;
            virtual void ConnectSender(SenderKey sk, const Process& proc) = 0;

            virtual void Initialize() = 0;

            virtual StorageTraceSet GetReceiverTraces(ReceiverKey rk) const = 0;
            virtual StorageTraceSet GetBroadcastTraces(SenderKey sk) const = 0;
            virtual StorageTraceSet GetRequestTraces(SenderKey sk) const = 0;

            virtual ~IInterconnect();
        protected:
            virtual void InitializeSender(SenderKey sk, const Process& proc) = 0;
        };


        template<typename Payload>
        class MessagePool;

        template<typename Payload>
        class Message : public Payload
        {
        private:
            union {
                MessagePool<Payload>* origin;
                Message<Payload>* next;
            };

        public:
            static void operator delete(void *p);
            static void* operator new(size_t sz);

            ~Message() {};

            template<typename...Args>
            Message(Args&&... args);

            Message* dup() const;

        protected:
            friend class MessagePool<Payload>;

            Message(const Message&) = delete;
        };

        template<typename Payload>
        class MessagePool {
            static Message<Payload>* g_FreeMessages;
            static  std::vector<Message<Payload>*> g_Messages;
        public:
            static void free(Message<Payload>* msg);

            static Message<Payload>* alloc();
        };

        template<typename Payload, typename T, bool (T::*TMethod)(Message<Payload>*)>
        inline receiver_cb_t<Payload>
        create_client_cb(T& obj)
        {
            return receiver_cb_t<Payload>::template create<T, TMethod>(obj);
        }

        template<typename T, void (T::*TMethod)(const Process&)>
        inline register_cb_t
        create_register_cb(T& obj)
        {
            return register_cb_t::template create<T, TMethod>(obj);
        }

    }
    namespace Serialization
    {
        template<typename Payload>
        struct serialize_trait<IC::Message<Payload>*>
            : public pointer_serializer<IC::Message<Payload> > {};
    }
}

#include "arch/Interconnect.hpp"

#endif
