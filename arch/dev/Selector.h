#ifndef SELECTOR_H
#define SELECTOR_H

#include "sim/delegate.h"
#include "sim/kernel.h"
#include "sim/storage.h"
#include "sim/inspect.h"

class Config;

namespace Simulator
{

    class ISelectorClient;

    class Selector : public Object, public Inspect::Interface<Inspect::Info>
    {
        static Selector*     m_singleton;

        SingleFlag m_doCheckStreams;

    public:

        enum StreamState
        {
            READABLE = 1,
            WRITABLE = 2,
        };

        Selector(const std::string& name, Object& parent, Clock& clock, Config& config);
        ~Selector();

        Process p_checkStreams;

        Result DoCheckStreams();

        bool RegisterStream(int fd, ISelectorClient& callback);
        bool UnregisterStream(int fd);

        static Selector* GetSelector() { return m_singleton; };

        /* debug */
        void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    };

    class ISelectorClient
    {
    public:
        virtual void OnStreamReady(int fd, Selector::StreamState state) = 0;
        virtual std::string GetSelectorClientName() = 0;
        virtual ~ISelectorClient() {}
    };


}

#endif
