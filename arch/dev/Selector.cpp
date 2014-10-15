#include "Selector.h"
#include <sim/except.h>

#include <map>
#include <iomanip>
#include <cstring>
#include <cerrno>
#include <fcntl.h>

#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 201101L
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <ev++.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

using namespace std;

namespace Simulator
{
    namespace Event
    {

        static
        struct ev_loop *evbase;

        static
        map<int, ev::io*>  handlers;

        static
        bool current_result = true;

        static
        size_t handler_count;

        static
        void selector_delegate_callback(ev::io& io, int revents)
        {
            // cerr << "I/O ready on fd " << fd << ", mode " << mode << endl;
            ISelectorClient* client = (ISelectorClient*)io.data;

            int st = 0;
            if (revents & ev::READ) st |= Selector::READABLE;
            if (revents & ev::WRITE) st |= Selector::WRITABLE;
            if (st != 0)
            {
                current_result &= client->OnStreamReady(io.fd, (Selector::StreamState)st);
                ++handler_count;
            }
        }
    }

    static map<int, int> fd_flags;

    void Selector::Enable()
    {
        for (auto& i : Event::handlers)
        {
            int fd = i.second->fd;
            int r = fcntl(fd, F_GETFL, 0);
            if (r == -1)
            {
                cerr << "Unable to get fd flags for " << fd << ": " << strerror(errno) << endl;
            }
            fd_flags[fd] = r;
            r = fcntl(fd, F_SETFL, r | O_NONBLOCK);
            if (r == -1)
            {
                cerr << "Unable to set non-blocking flags for " << fd << ": " << strerror(errno) << endl;
            }
        }
    }

    void Selector::Disable()
    {
        for (auto& i : Event::handlers)
        {
            int fd = i.second->fd;
            if (fd_flags.find(fd) == fd_flags.end())
                continue;
            int r = fcntl(fd, F_SETFL, fd_flags[fd] & ~O_NONBLOCK);
            if (r == -1)
            {
                cerr << "Unable to reset non-blocking flags for " << fd << ": " << strerror(errno) << endl;
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

        ev::io * &ev = Event::handlers[fd];

        assert(Event::evbase != NULL);

        ev = new ev::io(Event::evbase);
        ev->set<Event::selector_delegate_callback>(&callback);
        ev->start(fd, EV_READ|EV_WRITE);

        if (!m_doCheckStreams.IsSet())
            m_doCheckStreams.Set();
        return true;
    }

    bool Selector::UnregisterStream(int fd)
    {
        auto i = Event::handlers.find(fd);
        if (i == Event::handlers.end())
        {
            throw exceptf<InvalidArgumentException>(*this, "No handler registered for fd %d", fd);
        }

        // the following stops the event handler automatically
        delete i->second;
        Event::handlers.erase(i);

        if (Event::handlers.empty())
            m_doCheckStreams.Clear();

        return true;
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
            // Unfortunately, libev/kqueue only reports writability once
            // in a while, so we end up losing opportunities to send/write by calling
            // the loop too often.
            Event::current_result = true;
            Event::handler_count = 0;
            ev_loop(Event::evbase, EVLOOP_NONBLOCK);

            if (Event::handler_count == 0)
            {
                DebugIONetWrite("No I/O streams are ready.");
            }
        }

        return SUCCESS;
    }

    Selector* Selector::m_singleton = NULL;

    Selector::Selector(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent),
          InitStorage(m_doCheckStreams, clock, false),
          InitProcess(p_checkStreams, DoCheckStreams)
    {
        if (m_singleton != NULL)
        {
            throw InvalidArgumentException(*this, "More than one selector defined, previous at " + m_singleton->GetName());
        }
        m_singleton = this;

        // debug
        // event_enable_debug_mode();

        assert(Event::evbase == NULL);
        Event::evbase = ev_default_loop(0);
        if (Event::evbase == NULL)
        {
            throw InvalidArgumentException(*this, "Unable to initialize libev, bad LIBEV_FLAGS in environment?");
        }

        m_doCheckStreams.Sensitive(p_checkStreams);
    }

    Selector::~Selector()
    {
        for (auto& i : Event::handlers)
            delete i.second;

        Event::handlers.clear();

        m_singleton = NULL;
    }


    void Selector::Cmd_Info(ostream& out, const vector<string>& /* args */) const
    {
        out << "The selector is responsible for monitoring file descriptors and" << endl
            << "notifying other components when I/O is possible." << endl
            << endl
            << "Currently checking: " << (m_doCheckStreams.IsSet() ? "yes" : "no") << endl
            << "Checking method: ";

        unsigned backend = ev_backend(Event::evbase);
        switch(backend)
        {
        case ev::SELECT: cout << "select"; break;
        case ev::POLL: cout << "poll"; break;
        case ev::EPOLL: cout << "epoll"; break;
        case ev::KQUEUE: cout << "kqueue"; break;
        case ev::DEVPOLL: cout << "devpoll"; break;
        case ev::PORT: cout << "port"; break;
        default: cout << "unknown (" << backend << endl; break;
        }

        out << endl;

        if (Event::handlers.empty())
        {
            out << "No file descriptors registered." << endl;
        }
        else
        {
            out << endl
                << "FD  | Client" << endl
                << "----+------------------" << endl;
            for (auto& i : Event::handlers)
            {
                auto callback = (ISelectorClient*)i.second->data;
                assert(callback != NULL);
                out << setw(3) << setfill(' ') << i.first
                    << " | "
                    << callback->GetSelectorClientName()
                    << endl;
            }
        }

    }

}
