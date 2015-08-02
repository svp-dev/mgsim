// -*- c++ -*-
#ifndef IC_WIRENET_H
#define IC_WIRENET_H

#include "arch/Interconnect.h"
#include "sim/kernel.h"
#include <vector>

namespace Simulator
{

    namespace IC {

        template<typename Payload>
        class WireNet : public virtual Object, public virtual IInterconnect<Payload>
        {
        private:
            struct EndPoint {
                receiver_cb_t<Payload> deliver_cb;
                register_cb_t register_cb;
                traces_cb_t traces_cb;
                bool bcast_enabled;
                EndPoint() :
                    deliver_cb(), register_cb(), traces_cb(),
                    bcast_enabled(false) {}
                EndPoint(receiver_cb_t<Payload>&& dc, register_cb_t&& rc,
                         traces_cb_t&& rt,
                         bool be)
                    : deliver_cb(std::move(dc)),
                      register_cb(std::move(rc)),
                      traces_cb(std::move(rt)),
                      bcast_enabled(be) {}
            };
            std::vector<EndPoint> m_receivers;
            std::vector<const Process*> m_sender_procs;

        public:
            typedef typename IInterconnect<Payload>::MessageType MessageType;

            WireNet(const std::string& name, Object& parent)
                : Object(name, parent),
                  m_receivers(),
                  m_sender_procs()
            {}

            virtual ReceiverKey RegisterReceiver(const std::string& /*unused*/) override
            {
                auto rk = m_receivers.size();
                m_receivers.resize(rk + 1);
                return rk;
            }

            virtual void ConnectReceiver(ReceiverKey rk,
                                         receiver_cb_t<Payload>&& deliver_cb,
                                         register_cb_t&& register_cb,
                                         traces_cb_t&& rt,
                                         bool bcast_enabled) override
            {
                assert(rk < m_receivers.size());
                m_receivers[rk] = EndPoint{ std::move(deliver_cb),
                                            std::move(register_cb),
                                            std::move(rt),
                                            bcast_enabled };
            }

            virtual SenderKey RegisterSender(const std::string& /*unused*/) override
            {
                auto sk = m_sender_procs.size();
                m_sender_procs.resize(sk + 1);
                return sk;
            }

            virtual void ConnectSender(SenderKey sk, const Process& proc) override
            {
                assert(sk < m_sender_procs.size());
                assert(m_sender_procs[sk] == 0);
                m_sender_procs[sk] = &proc;
            }

            virtual void Initialize() override
            {
                for (SenderKey sk = 0; sk < m_sender_procs.size(); ++sk)
                {
                    auto p = m_sender_procs[sk];
                    if (p) {
                        for (auto & e : m_receivers)
                            if (e.register_cb)
                                e.register_cb(*p);
                        this->InitializeSender(sk, *p);
                    }
                }
            }

            bool IsBroadcastReceiver(ReceiverKey dst) const
            {
                assert(dst < m_receivers.size());
                return m_receivers[dst].bcast_enabled;
            }

            virtual StorageTraceSet GetReceiverTraces(ReceiverKey rk) const override
            {
                assert(rk < m_receivers.size());
                return m_receivers[rk].traces_cb();
            }

            virtual StorageTraceSet GetRequestTraces(SenderKey /*unused*/) const override
            {
                StorageTraceSet ret;
                for (size_t rk = 0; rk < m_receivers.size(); ++rk)
                    ret ^= GetReceiverTraces(rk);
                return ret;
            }
            virtual StorageTraceSet GetBroadcastTraces(SenderKey /*unused*/) const override
            {
                StorageTraceSet ret;
                for (size_t rk = 0; rk < m_receivers.size(); ++rk)
                    if (m_receivers[rk].bcast_enabled)
                        ret *= GetReceiverTraces(rk);
                return ret;
            }

            virtual bool SendBroadcast(SenderKey /*unused*/, MessageType* msg) override
            {
                bool res = true;
                for (auto &r : m_receivers)
                    if (r.bcast_enabled)
                        res = res & r.deliver_cb(msg->dup());
                COMMIT{ delete msg; }
                return res;
            }

            virtual bool SendMessage(SenderKey /*unused*/, ReceiverKey dst, MessageType* msg) override
            {
                assert (dst < m_receivers.size());
                return m_receivers[dst].deliver_cb(msg);
            }

        protected:
            virtual void InitializeSender(SenderKey /*unused*/, const Process& /*unused*/) override
            {}


        };
    }
}

#endif
