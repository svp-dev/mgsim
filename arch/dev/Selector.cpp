#include "Selector.h"
#include "sim/except.h"
#include <map>
#include <iomanip>
#include <event2/event.h>

using namespace std;

namespace Simulator
{
    namespace Event
    {
        static
        map<int, struct event*>  handlers;
        
        static
        struct event_base* evbase = NULL;
        
        static
        bool current_result = true;

        static
        size_t handler_count;

        static
        void selector_delegate_callback(int fd, short mode, void *arg)
        {
            // cerr << "I/O ready on fd " << fd << ", mode " << mode << endl;
            ISelectorClient* client = (ISelectorClient*)arg;
            
            int st = 0;
            if (mode & EV_READ) st |= Selector::READABLE;
            if (mode & EV_WRITE) st |= Selector::WRITABLE;
            if (st != 0)
            {
                current_result &= client->OnStreamReady(fd, (Selector::StreamState)st);
                ++handler_count;
            }
        }
    }

    Selector& Selector::GetSelector()
    {
        assert(m_singleton != NULL);
        return *m_singleton;
    }

    bool Selector::RegisterStream(int fd, ISelectorClient& callback)
    {
        if (Event::handlers.find(fd) != Event::handlers.end())
        {
            throw exceptf<InvalidArgumentException>(*this, "Handler already registered for fd %d", fd);
        }

        struct event * &ev = Event::handlers[fd];

        ev = event_new(Event::evbase, fd, EV_WRITE|EV_READ|EV_PERSIST, &Event::selector_delegate_callback, &callback);

        int res = event_add(ev, NULL);
        
        if (res != 0)
        {
            DebugIOWrite("Unable to register handler for fd %d", fd);
            return false;
        }
        else
        {
            if (!m_doCheckStreams.IsSet())
                m_doCheckStreams.Set();
            return true;
        }
    }

    bool Selector::UnregisterStream(int fd)
    {
        map<int, struct event*>::iterator i = Event::handlers.find(fd);
        if (i == Event::handlers.end())
        {
            throw exceptf<InvalidArgumentException>(*this, "No handler registered for fd %d", fd);
        }
        
        int res = event_del(i->second);

        if (res != 0)
        {
            DebugIOWrite("Unable to unregister handler for fd %d", fd);
            return false;
        }
        else
        {
            event_free(i->second);
            Event::handlers.erase(i);
            if (Event::handlers.empty())
                m_doCheckStreams.Clear();
            return true;
        }
    }

    Result Selector::DoCheckStreams()
    {
        assert(Event::evbase != NULL);

        // we catch up I/O stalls only at the next cycle because we
        // cannot really check for I/O events 3 times per cycle, and
        // we cannot report failure at the commit phase if it was not
        // also reported at the check phase.
        if (Event::current_result == false)
        {
            DeadlockWrite("Some stream handler could not process their I/O event");
            Event::current_result = true;
            return FAILED;
        }
        
        // cerr << GetClock().GetCycleNo() << ": Checking for I/O stream availability" << endl;

        COMMIT { 
            // in principle we should be able to run event_base_loop once per
            // kernel phase, and only actually do the I/O on the commit phase.
            // Unfortunately, libevent/kqueue only reports writability once
            // in a while, so we end up losing opportunities to send/write by calling
            // the loop too often.
            Event::current_result = true;
            Event::handler_count = 0;
            event_base_loop(Event::evbase, EVLOOP_ONCE | EVLOOP_NONBLOCK); 

            if (Event::handler_count == 0)
            {
                DebugIOWrite("No I/O streams are ready.");
            }
        }

        // cerr << "res " << Event::current_result << ", count " << Event::handler_count << endl;

        return SUCCESS;
    }
    
    Selector* Selector::m_singleton = NULL;

    Selector::Selector(const std::string& name, Object& parent, Clock& clock, Config& config)
        : Object(name, parent, clock),
          m_doCheckStreams("f_checking", *this, clock, false),
          p_checkStreams(*this, "check-streams", delegate::create<Selector, &Selector::DoCheckStreams>(*this))
    { 
        if (m_singleton != NULL)
        {
            throw InvalidArgumentException(*this, "More than one selector defined, previous at " + m_singleton->GetFQN());
        }
        m_singleton = this;

        // debug
        // event_enable_debug_mode();

        assert(Event::evbase == NULL);
        Event::evbase = event_base_new();

        m_doCheckStreams.Sensitive(p_checkStreams);
    }

    Selector::~Selector()
    {
        for (map<int, struct event*>::const_iterator i = Event::handlers.begin(); i != Event::handlers.end(); ++i)
        {
            // event_free automatically calls event_del
            event_free(i->second);
        }
        event_base_free(Event::evbase);
        m_singleton = NULL;
    }


    void Selector::Cmd_Info(ostream& out, const vector<string>& /* args */) const
    {
        out << "The selector is responsible for monitoring file descriptors and" << endl
            << "notifying other components when I/O is possible." << endl
            << endl
            << "Currently checking: " << (m_doCheckStreams.IsSet() ? "yes" : "no") << endl
            << "Checking method: " << event_base_get_method(Event::evbase) << endl;

        if (Event::handlers.empty())
        {
            out << "No file descriptors registered." << endl;
        }
        else
        {
            out << endl
                << "FD  | Client" << endl
                << "----+------------------" << endl;
            for (map<int, struct event*>::const_iterator i = Event::handlers.begin(); i != Event::handlers.end(); ++i)
            {
                ISelectorClient *callback = (ISelectorClient*)event_get_callback_arg(i->second);
                assert(callback != NULL);
                out << setw(3) << setfill(' ') << i->first
                    << " | "
                    << callback->GetSelectorClientName()
                    << endl;
            }
        }

    }

}
