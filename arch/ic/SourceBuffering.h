// -*- c++ -*-
#ifndef IC_SOURCE_BUFFERING_H
#define IC_SOURCE_BUFFERING_H

#include "arch/Interconnect.h"
#include "sim/kernel.h"
#include "sim/buffer.h"
#include "sim/delegate_closure.h"

namespace Simulator
{

    namespace IC {

        template<typename BaseIC>
        class SourceBuffering
            : public virtual Object,
              public virtual IInterconnect<typename BaseIC::PayloadType>,
              public BaseIC
        {
        public:
            typedef typename BaseIC::MessageType MessageType;
        private:
            struct OutgoingMessage
            {
                enum { NORMAL, BROADCAST } type;
                ReceiverKey dst;
                MessageType *msg;

                SERIALIZE(a) { (void)a; }
            };

            struct EndPoint
            {
                Buffer<OutgoingMessage>* m_buffer;
                Process* m_send;
                bool     m_buffering_enabled;
                Clock*   m_clock;


            };
            std::vector<EndPoint> m_endpoints;

            Result DoSend(SenderKey sk)
            {
                auto& b = *m_endpoints[sk].m_buffer;
                auto& m = b.Front();

                switch(m.type)
                {
                case OutgoingMessage::NORMAL:
                    if (!this->BaseIC::SendMessage(sk, m.dst, m.msg))
                    {
                        DeadlockWrite("Unable to send message %zu -> %zu", (size_t)sk, (size_t)m.dst);
                        return FAILED;
                    }
                    break;
                case OutgoingMessage::BROADCAST:
                    if (!this->BaseIC::SendBroadcast(sk, m.msg))
                    {
                        DeadlockWrite("Unable to send broadcast from %zu", (size_t)sk);
                        return FAILED;
                    }
                    break;
                }

                b.Pop();

                return SUCCESS;
            }

        public:

            SourceBuffering(const std::string& name, Object& parent)
                : Object(name, parent),
                  BaseIC(name, parent),
                  m_endpoints()
            {
            }

            ~SourceBuffering()
            {
                for (auto& e : m_endpoints)
                {
                    if (e.m_buffering_enabled)
                    {
                        delete e.m_send;
                        delete e.m_buffer;
                    }
                }
            }

            virtual StorageTraceSet GetRequestTraces(SenderKey sk) const override
            {
                if (!m_endpoints[sk].m_buffering_enabled)
                {
                    return this->BaseIC::GetRequestTraces(sk);
                }
                else
                {
                    return *m_endpoints[sk].m_buffer;
                }
            }

            virtual StorageTraceSet GetBroadcastTraces(SenderKey sk) const override
            {
                if (!m_endpoints[sk].m_buffering_enabled)
                {
                    return this->BaseIC::GetBroadcastTraces(sk);
                }
                else
                {
                    return *m_endpoints[sk].m_buffer;
                }
            }

            virtual bool SendMessage(SenderKey src, ReceiverKey dst, MessageType* msg) override
            {
                assert(src < m_endpoints.size());

                if (!m_endpoints[src].m_buffering_enabled)
                {
                    return this->BaseIC::SendMessage(src, dst, msg);
                }
                else
                {
                    if (!m_endpoints[src].m_buffer->Push(OutgoingMessage { OutgoingMessage::NORMAL, dst, msg }))
                    {
                        DeadlockWrite("Unable to queue outgoing message %zu -> %zu",
                                      (size_t)src, (size_t)dst);
                        return false;
                    }
                    return true;
                }
            }

            virtual bool SendBroadcast(SenderKey src, MessageType* msg) override
            {
                assert(src < m_endpoints.size());

                if (!m_endpoints[src].m_buffering_enabled)
                {
                    return this->BaseIC::SendBroadcast(src, msg);
                }
                else
                {
                    if (!m_endpoints[src].m_buffer->Push(OutgoingMessage { OutgoingMessage::BROADCAST, 0, msg }))
                    {
                        DeadlockWrite("Unable to queue outgoing broadcast from %zu",
                                      (size_t)src);
                        return false;
                    }
                    return true;
                }
            }

            virtual Clock& GetSenderClock(SenderKey sk) const override
            {
                return *m_endpoints[sk].m_clock;
            }

            virtual SenderKey RegisterSender(const std::string& lname) override
            {
                SenderKey sk = this->BaseIC::RegisterSender(lname);
                if (sk >= m_endpoints.size())
                    m_endpoints.resize(sk + 1);
                EndPoint& e = m_endpoints[sk];

                const std::string name = "in" + std::to_string(sk);
                e.m_clock = &GetKernel()->CreateClock(GetKernel()->GetConfig()->template getValue<Clock::Frequency>(*this, name, "InputFreq"));

                e.m_buffering_enabled = GetKernel()->GetConfig()->template getValueOrDefault<bool>(lname, "IOSourceBufferingEnabled", true);

                if (e.m_buffering_enabled)
                {
                    auto bname = name + ".b_buffer";
                    e.m_buffer = new Buffer<OutgoingMessage>(bname, *this,
                                                             this->BaseIC::GetSenderClock(sk),
                                                             GetKernel()->GetConfig()->template getValue<BufferSize>(*this, bname, "BufferSize"));
                    auto pname = name + ".p_send";
                    e.m_send = new Process(*this, pname,
                                           closure<Result>::adapter<Result>::capture<SenderKey>::create<SourceBuffering, &SourceBuffering::DoSend>(*this, sk));
                    this->BaseIC::ConnectSender(sk, *e.m_send);
                    e.m_buffer->Sensitive(*e.m_send);
                }
                return sk;
            }

            virtual void ConnectSender(SenderKey sk, const Process& proc) override
            {
                assert(sk < m_endpoints.size());
                if (!m_endpoints[sk].m_buffering_enabled)
                {
                    this->BaseIC::ConnectSender(sk, proc);
                }
            }

            virtual void Initialize() override
            {
                this->BaseIC::Initialize();
                for (size_t sk = 0; sk < m_endpoints.size(); ++sk)
                    if (m_endpoints[sk].m_buffering_enabled)
                        m_endpoints[sk].m_send->SetStorageTraces(this->BaseIC::GetRequestTraces(sk) ^ this->BaseIC::GetBroadcastTraces(sk));
            }
        };
    }
}

#endif
