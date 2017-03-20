// -*- c++ -*-
#ifndef IC_ENDPOINTREGISTRY_H
#define IC_ENDPOINTREGISTRY_H

#include "arch/Interconnect.h"
#include "sim/kernel.h"
#include <iomanip>
#include <map>

namespace Simulator
{

    namespace IC {

        template<typename BaseIC>
        class EndPointRegistry
            : public virtual Object, public virtual IInterconnect<typename BaseIC::PayloadType>,
              public BaseIC, public virtual Inspect::Interface<Inspect::Info>
        {
        protected:
            std::vector<std::string> m_receivers;
            std::vector<std::string> m_senders;

        public:
            EndPointRegistry(const std::string& name, Object& parent)
                : Object(name, parent),
                  BaseIC(name, parent),
                  m_receivers(),
                  m_senders()
                  {}

            virtual ReceiverKey RegisterReceiver(const std::string& lname) override
            {
                auto rk = this->BaseIC::RegisterReceiver(lname);
                if (rk >= m_receivers.size())
                    m_receivers.resize(rk + 1);
                m_receivers[rk] = lname;
                return rk;
            }

            virtual SenderKey RegisterSender(const std::string& lname) override
            {
                auto sk = this->BaseIC::RegisterSender(lname);
                if (sk >= m_senders.size())
                    m_senders.resize(sk + 1);
                m_senders[sk] = lname;
                return sk;
            }

            virtual void Cmd_Info(std::ostream& out, const std::vector<std::string>& /*args*/) const override
            {
                out << "List of receivers:" << std::endl;
                for (ReceiverKey k = 0; k < m_receivers.size(); ++k)
                    out << k << " : " << m_receivers[k]
                        << (BaseIC::IsBroadcastReceiver(k) ? " (broadcast receiver)" : "")
                        << std::endl;
                out << "List of senders:" << std::endl;
                for (SenderKey k = 0; k < m_senders.size(); ++k)
                    out << k << " : " << m_senders[k] << std::endl;
            }

        };
    }
}

#endif
