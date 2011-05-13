#include "Selector.h"
#include "sim/except.h"
#include <map>
#include <iomanip>
#include <event.h>

using namespace std;

namespace Simulator
{
    namespace Event
    {
        static
        map<int, struct event>  handlers;
        
        static
        struct event_base* evbase = NULL;
        
        static
        void selector_delegate_callback(int fd, short mode, void *arg)
        {
            ISelectorClient* client = (ISelectorClient*)arg;
            
            int st = 0;
            if (mode & EV_READ) st |= Selector::READABLE;
            if (mode & EV_WRITE) st |= Selector::WRITABLE;
            
            client->OnStreamReady(fd, (Selector::StreamState)st);
        }
    }

    bool Selector::RegisterStream(int fd, ISelectorClient& callback)
    {
        if (Event::handlers.find(fd) != Event::handlers.end())
        {
            throw exceptf<InvalidArgumentException>(*this, "Handler already registered for fd %d", fd);
        }

        struct event *ev = &Event::handlers[fd];

        event_set(ev, fd, EV_READ|EV_WRITE, &Event::selector_delegate_callback, &callback);
        event_base_set(Event::evbase, ev);

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
        map<int, struct event>::iterator i = Event::handlers.find(fd);
        if (i == Event::handlers.end())
        {
            throw exceptf<InvalidArgumentException>(*this, "No handler registered for fd %d", fd);
        }
        
        int res = event_del(&(i->second));

        if (res != 0)
        {
            DebugIOWrite("Unable to unregister handler for fd %d", fd);
            return false;
        }
        else
        {
            Event::handlers.erase(i);
            if (Event::handlers.empty())
                m_doCheckStreams.Clear();
            return true;
        }
    }

    Result Selector::DoCheckStreams()
    {
        assert(Event::evbase != NULL);
        COMMIT
        {
            event_base_loop(Event::evbase, EVLOOP_ONCE | EVLOOP_NONBLOCK);
        }
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
        assert(Event::evbase == NULL);
        Event::evbase = event_base_new();

        m_doCheckStreams.Sensitive(p_checkStreams);
    }

    Selector::~Selector()
    {
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
            for (map<int, struct event>::const_iterator i = Event::handlers.begin(); i != Event::handlers.end(); ++i)
            {
                ISelectorClient *callback = (ISelectorClient*)event_get_callback_arg(&i->second);
                assert(callback != NULL);
                out << setw(3) << setfill(' ') << i->first
                    << " | "
                    << callback->GetSelectorClientName()
                    << endl;
            }
        }

    }

}
