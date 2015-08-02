// -*- c++ -*-
#ifndef IC_DESTINATION_BUFFERING_H
#define IC_DESTINATION_BUFFERING_H

#include "arch/Interconnect.h"
#include "sim/kernel.h"
#include "sim/buffer.h"
#include "sim/delegate_closure.h"

namespace Simulator
{

    namespace IC {

        template<typename BaseIC>
        class DestinationBuffering
            : public virtual Object,
              public virtual IInterconnect<typename BaseIC::PayloadType>,
              public BaseIC
        {
        public:
            typedef typename BaseIC::MessageType MessageType;
            typedef typename BaseIC::PayloadType PayloadType;
        private:
            struct EndPoint
            {
                Buffer<MessageType*>* m_buffer;
                Process* m_receive;
                receiver_cb_t<PayloadType> m_deliver;
                bool     m_buffering_enabled;

                EndPoint()
                    : m_buffer(0), m_receive(0), m_deliver(), m_buffering_enabled() {}
                EndPoint(EndPoint&& e)
                    : m_buffer(e.m_buffer), m_receive(e.m_receive),
                      m_deliver(std::move(e.m_deliver)),
                      m_buffering_enabled(e.m_buffering_enabled) {}
                EndPoint(const EndPoint&) = delete;
                EndPoint& operator=(const EndPoint&) = delete;
            };
            std::vector<EndPoint> m_endpoints;

            Result DoReceive(ReceiverKey rk)
            {
                assert(rk < m_endpoints.size());
                auto& e = m_endpoints[rk];
                auto& b = *e.m_buffer;
                auto m = b.Front();

                if (!e.m_deliver(m))
                {
                    DeadlockWrite("Unable to deliver message to %zu", (size_t)rk);
                    return FAILED;
                }
                b.Pop();
                return SUCCESS;
            }

            void RegisterSenderProcess(const Process& /* ignore */)
            {}

            bool ReceiveMessage(ReceiverKey rk, MessageType* msg)
            {
                if (!m_endpoints[rk].m_buffer->Push(msg))
                {
                    DeadlockWrite("Unable to push incoming message to %zu", (size_t)rk);
                    return false;
                }
                return true;
            }



        public:

            DestinationBuffering(const std::string& name, Object& parent)
                : Object(name, parent),
                  BaseIC(name, parent),
                  m_endpoints()
            {
            }

            ~DestinationBuffering()
            {
                for (auto& e : m_endpoints)
                {
                    if (e.m_buffering_enabled)
                    {
                        delete e.m_receive;
                        delete e.m_buffer;
                    }
                }
            }

            virtual StorageTraceSet GetReceiverTraces(ReceiverKey rk) const override
            {
                if (!m_endpoints[rk].m_buffering_enabled)
                {
                    return this->BaseIC::GetReceiverTraces(rk);
                }
                else
                {
                    return *m_endpoints[rk].m_buffer;
                }
            }

            virtual ReceiverKey RegisterReceiver(const std::string& lname) override
            {
                ReceiverKey rk = this->BaseIC::RegisterReceiver(lname);
                if (rk >= m_endpoints.size())
                    m_endpoints.resize(rk + 1);
                EndPoint& e = m_endpoints[rk];

                const std::string name = "out" + std::to_string(rk);
                Clock& clock = GetKernel()->CreateClock(GetKernel()->GetConfig()->template getValue<Clock::Frequency>(*this, name, "OutputFreq"));

                e.m_buffering_enabled = GetKernel()->GetConfig()->template getValueOrDefault<bool>(lname, "IOReceiverBufferingEnabled", true);

                if (e.m_buffering_enabled)
                {
                    auto bname = name + ".b_buffer";
                    e.m_buffer = new Buffer<MessageType*>(bname, *this,
                                                          clock,
                                                          GetKernel()->GetConfig()->template getValue<BufferSize>(*this, bname, "BufferSize"));
                    auto pname = name + ".p_receive";
                    e.m_receive = new Process(*this, pname,
                                              closure<Result>::adapter<Result>::capture<ReceiverKey>::create<DestinationBuffering, &DestinationBuffering::DoReceive>(*this, rk));
                    e.m_buffer->Sensitive(*e.m_receive);
                }
                return rk;
            }

            virtual void ConnectReceiver(ReceiverKey rk,
                                         receiver_cb_t<PayloadType>&& deliver_cb,
                                         register_cb_t&& register_cb,
                                         traces_cb_t&& req_traces_cb,
                                         bool bcast_enabled = true) override
            {
                assert(rk < m_endpoints.size());
                auto &e = m_endpoints[rk];
                if (!e.m_buffering_enabled)
                {
                    this->BaseIC::ConnectReceiver(rk,
                                                  std::move(deliver_cb),
                                                  std::move(register_cb),
                                                  std::move(req_traces_cb),
                                                  bcast_enabled);
                }
                else
                {
                    e.m_deliver = std::move(deliver_cb);
                    this->BaseIC::ConnectReceiver(rk,
                                                  closure<bool, MessageType*>::
                                                  template adapter<bool, MessageType*>::
                                                  template capture<ReceiverKey>::
                                                  template create<DestinationBuffering, &DestinationBuffering::ReceiveMessage>(*this, rk),
                                                  register_cb_t::create<DestinationBuffering, &DestinationBuffering::RegisterSenderProcess>(*this),
                                                  std::move(req_traces_cb),
                                                  bcast_enabled);
                    register_cb(*e.m_receive);
                }
            }


            virtual void Initialize() override
            {
                this->BaseIC::Initialize();
                for (size_t rk = 0; rk < m_endpoints.size(); ++rk)
                    if (m_endpoints[rk].m_buffering_enabled)
                        m_endpoints[rk].m_receive->SetStorageTraces(this->BaseIC::GetReceiverTraces(rk));
            }
        };
    }
}

#endif
