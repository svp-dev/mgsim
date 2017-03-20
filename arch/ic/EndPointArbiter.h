// -*- c++ -*-
#ifndef IC_ENDPOINT_ARBITER_H
#define IC_ENDPOINT_ARBITER_H

#include "arch/Interconnect.h"
#include "sim/ports.h"
#include "sim/kernel.h"
#include "sim/config.h"

namespace Simulator
{

    namespace IC {

        template<typename BaseIC, typename Arbitrator = CyclicArbitratedPort>
        class EndPointArbiter
            : public virtual Object,
              public virtual IInterconnect<typename BaseIC::PayloadType>,
              public BaseIC
        {
        private:
            typedef ArbitratedService<Arbitrator> service_t;
            std::vector<service_t*> m_ports;
            Clock& m_clock;

        public:
            typedef typename BaseIC::MessageType MessageType;

            EndPointArbiter(const std::string& name, Object& parent)
                : Object(name, parent),
                  BaseIC(name, parent),
                  m_ports(),
                  m_clock(GetKernel()->CreateClock(GetKernel()->GetConfig()->template getValue<Clock::Frequency>(*this, "CrossbarFreq")))
            {}

            ~EndPointArbiter()
            {
                for (auto s : m_ports)
                    delete s;
            }
            virtual Clock& GetSenderClock(SenderKey /*ignore*/) const override
            {
                return m_clock;
            }

            virtual ReceiverKey RegisterReceiver(const std::string& lname) override
            {
                auto rk = this->BaseIC::RegisterReceiver(lname);
                if (rk >= m_ports.size())
                    m_ports.resize(rk + 1);
                auto pname = BaseIC::GetName() + ".out" + std::to_string(rk) + ".p_service";
                m_ports[rk] = new service_t(m_clock, pname);
                return rk;
            }

            virtual bool SendBroadcast(SenderKey src, MessageType *msg) override
            {
                for (ReceiverKey i = 0; i < m_ports.size(); ++i)
                {
                    auto p = m_ports[i];
                    if (BaseIC::IsBroadcastReceiver(i))
                        if (!p->Invoke())
                        {
                            DeadlockWrite("Unable to acquire port %zu for crossbar broadcast access by %zu",
                                          (size_t)i, (size_t)src);
                            return false;
                        }
                }

                return this->BaseIC::SendBroadcast(src, msg);
            }

            virtual bool SendMessage(SenderKey src, ReceiverKey dst, MessageType* msg) override
            {
                assert (dst < m_ports.size());
                auto p = m_ports[dst];
                if (!p->Invoke())
                {
                    DeadlockWrite("Unable to acquire port %zu for crossbar access by %zu",
                                  (size_t)dst, (size_t)src);
                    return false;
                }

                return this->BaseIC::SendMessage(src, dst, msg);
            }

        protected:
            virtual void InitializeSender(SenderKey /*unused*/, const Process& proc) override
            {
                for (auto p : m_ports)
                    p->AddProcess(proc);
            }
        };
    }
}

#endif
