// -*- c++ -*-
#ifndef IC_SHAREDMEDIUM_H
#define IC_SHAREDMEDIUM_H

#include "arch/Interconnect.h"
#include "sim/ports.h"
#include "sim/kernel.h"
#include "sim/config.h"
#include <map>
#include <tuple>

namespace Simulator
{

    namespace IC {

        template<typename BaseIC, typename Arbitrator = CyclicArbitratedPort>
        class SharedMedium
            : public virtual Object,
              public virtual IInterconnect<typename BaseIC::PayloadType>,
              public BaseIC
        {
            Clock& m_clock;
            ArbitratedService<Arbitrator> p_bus;

        public:
            typedef typename BaseIC::MessageType MessageType;

            SharedMedium(const std::string& name, Object& parent)
                : Object(name, parent),
                  BaseIC(name, parent),
                  m_clock(GetKernel()->CreateClock(GetKernel()->GetConfig()->template getValue<Clock::Frequency>(*this, "BusFreq"))),
                  p_bus(m_clock, GetName() + ".p_bus")
            {}

            virtual Clock& GetSenderClock(SenderKey /*ignore*/) const override
            {
                return m_clock;
            }

            virtual bool SendBroadcast(SenderKey src, MessageType* msg) override
            {
                if (!p_bus.Invoke())
                {
                    DeadlockWrite("Unable to acquire port for broadcast by %zu",
                                  (size_t)src);
                    return false;
                }

                return this->BaseIC::SendBroadcast(src, msg);
            }

            virtual bool SendMessage(SenderKey src, ReceiverKey dst, MessageType* msg) override
            {
                if (!p_bus.Invoke())
                {
                    DeadlockWrite("Unable to acquire port for bus access for %zu -> %zu",
                                  (size_t)src, (size_t)dst);
                    return false;
                }

                return this->BaseIC::SendMessage(src, dst, msg);
            }

        protected:
            virtual void InitializeSender(SenderKey /*unused*/, const Process& proc) override
            {
                p_bus.AddProcess(proc);
            }
        };
    }
}

#endif
