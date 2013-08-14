#include "IONotificationMultiplexer.h"
#include "DRISC.h"
#include <sim/config.h>

#include <sstream>
#include <iomanip>

using namespace std;

namespace Simulator
{
namespace drisc
{

IONotificationMultiplexer::IONotificationMultiplexer(const string& name, IOInterface& parent, Clock& clock,
                                                     size_t numChannels,
                                                     Config& config)
    : Object(name, parent, clock),
      m_writebacks(numChannels, 0),
      m_mask(numChannels, false),
      m_interrupts(numChannels, 0),
      m_notifications(numChannels, 0),
      m_services(numChannels, 0),
      m_lastNotified(0),
      p_IncomingNotifications(*this, "received-notifications", delegate::create<IONotificationMultiplexer, &IONotificationMultiplexer::DoReceivedNotifications>(*this))
{
    BufferSize nqs = config.getValue<BufferSize>(*this, "NotificationQueueSize");

    for (size_t i = 0; i < numChannels; ++i)
    {
        {
            stringstream ss;
            ss << "wb" << i;
            m_writebacks[i] = new Register<RegAddr>(ss.str(), *this, clock);
            m_writebacks[i]->Sensitive(p_IncomingNotifications);
        }
        {
            stringstream ss;
            ss << "latch" << i;
            m_interrupts[i] = new SingleFlag(ss.str(), *this, clock, false);
            m_interrupts[i]->Sensitive(p_IncomingNotifications);
        }
        {
            stringstream ss;
            ss << "b_notification" << i;
            m_notifications[i] = new Buffer<Integer>(ss.str(), *this, clock, nqs);
            m_notifications[i]->Sensitive(p_IncomingNotifications);
        }
        {
            stringstream ss;
            ss << "p_service" << i;
            m_services[i] = new ArbitratedService<>(*this, clock, ss.str());
        }
    }

}

IONotificationMultiplexer::~IONotificationMultiplexer()
{
    for (size_t i = 0; i < m_writebacks.size(); ++i)
    {
        delete m_writebacks[i];
        delete m_interrupts[i];
        delete m_notifications[i];
        delete m_services[i];
    }
}

bool IONotificationMultiplexer::ConfigureChannel(IONotificationChannelID which, Integer mode)
{
    assert(which < m_writebacks.size());

    COMMIT { m_mask[which] = !!mode; }

    DebugIOWrite("Configuring channel %u to state: %s", (unsigned)which, (!!mode) ? "enabled" : "disabled");

    return true;
}


bool IONotificationMultiplexer::SetWriteBackAddress(IONotificationChannelID which, const RegAddr& addr)
{
    assert(which < m_writebacks.size());

    if (!m_services[which]->Invoke())
    {
        DeadlockWrite("Unable to acquire service for channel %u", (unsigned)which);
        return false;
    }

    if (!m_writebacks[which]->Empty())
    {
        // some thread is already waiting, do not
        // allow another one to wait as well!
        return false;
    }

    m_writebacks[which]->Write(addr);
    return true;
}

bool IONotificationMultiplexer::OnInterruptRequestReceived(IONotificationChannelID from)
{
    assert(from < m_interrupts.size());

    if (!m_mask[from])
    {
        DebugIONetWrite("Ignoring interrupt for disabled channel %u", (unsigned)from);
        return true;
    }
    else
    {
        DebugIONetWrite("Activating interrupt status for channel %u", (unsigned)from);
        return m_interrupts[from]->Set();
    }
}

bool IONotificationMultiplexer::OnNotificationReceived(IONotificationChannelID from, Integer tag)
{
    assert(from < m_notifications.size());

    if (!m_mask[from])
    {
        DebugIONetWrite("Ignoring notification for disabled channel %u (tag %#016llx)", (unsigned)from, (unsigned long long)tag);
        return true;
    }
    else
    {
        DebugIONetWrite("Queuing notification for channel %u (tag %#016llx)", (unsigned)from, (unsigned long long)tag);
        return m_notifications[from]->Push(tag);
    }
}


Result IONotificationMultiplexer::DoReceivedNotifications()
{
    size_t i, j;
    bool   notification_ready = false;
    bool   pending_notifications = false;

    /* Search for a notification to signal back to the processor.
     The following two loops implement a circular lookup through
    all devices -- round robin delivery to ensure fairness. */
    for (j = m_lastNotified; j < m_lastNotified + m_interrupts.size(); ++j)
    {
        i = j % m_interrupts.size();

        if (m_interrupts[i]->IsSet() || !m_notifications[i]->Empty())
        {
            pending_notifications = true;
            if (!m_mask[i] || !m_writebacks[i]->Empty())
            {
                notification_ready = true;
                break;
            }
        }
        else if (!m_mask[i] && !m_writebacks[i]->Empty())
        {
            // channel was disabled + no pending interrupt/notification + still a listener active,
            // so we need to release the listener otherwise it will deadlock.
            notification_ready = true;
            break;
        }
    }

    if (!notification_ready)
    {
        if (pending_notifications)
        {
            DebugIOWrite("Some notification is ready but no active listener exists.");
        }

        // Nothing to do
        return SUCCESS;
    }

    /* A notification was found, try to notify the processor */

    if (!m_services[i]->Invoke())
    {
        DeadlockWrite("Unable to acquire service for channel %zu", i);
        return FAILED;
    }

    if (m_writebacks[i]->Empty())
    {
        // we arrived here because a channel was disabled
        // but there is still an event pending on it.
        assert(m_mask[i] == false);

        m_interrupts[i]->Clear();

        if (!m_notifications[i]->Empty())
        {
            m_notifications[i]->Pop();
        }

        DebugIOWrite("Drained one event on disabled channel %zu", i);
    }
    else
    {
        // we arrive here because there is a listener
        // and either the channel is disabled, or it is enabled and there is some event.

        const RegAddr& addr = m_writebacks[i]->Read();

        auto& cpu = GetDRISC();
        auto& regFile = cpu.GetRegisterFile();

        if (!regFile.p_asyncW.Write(addr))
        {
            DeadlockWrite("Unable to acquire port to write back %s", addr.str().c_str());
            return FAILED;
        }

        RegValue regvalue;
        if (!regFile.ReadRegister(addr, regvalue))
        {
            DeadlockWrite("Unable to read register %s", addr.str().c_str());
            return FAILED;
        }

        if (regvalue.m_state == RST_FULL)
        {
            // Rare case: the request info is still in the pipeline, stall!
            DeadlockWrite("Register %s is not yet written for read completion", addr.str().c_str());
            return FAILED;
        }

        if (regvalue.m_state != RST_PENDING && regvalue.m_state != RST_WAITING)
        {
            // We're too fast, wait!
            DeadlockWrite("I/O notification delivered before register %s was cleared", addr.str().c_str());
            return FAILED;
        }

        // Now write
        LFID fid = regvalue.m_memory.fid;
        regvalue.m_state = RST_FULL;
        Integer value;
        const char * type;
        if (m_interrupts[i]->IsSet())
        {
            // Interrupts have priority over notifications
            value = 0;
            type = "interrupt";
        }
        else if (!m_notifications[i]->Empty())
        {
            value = m_notifications[i]->Front();
            type = "notification";
        }
        else
        {
            value = 0;
            type = "draining disabled channel";
        }

        switch (addr.type) {
        case RT_INTEGER: regvalue.m_integer       = value; break;
        case RT_FLOAT:   regvalue.m_float.integer = value; break;
        default: UNREACHABLE;
        }

        if (!regFile.WriteRegister(addr, regvalue, true))
        {
            DeadlockWrite("Unable to write register %s", addr.str().c_str());
            return FAILED;
        }

        auto& alloc = cpu.GetAllocator();
        if (!alloc.DecreaseFamilyDependency(fid, FAMDEP_OUTSTANDING_READS))
        {
            DeadlockWrite("Unable to decrement outstanding reads on F%u", (unsigned)fid);
            return FAILED;
        }

        m_writebacks[i]->Clear();

        if (m_interrupts[i]->IsSet())
        {
            m_interrupts[i]->Clear();
        }
        else if (!m_notifications[i]->Empty())
        {
            m_notifications[i]->Pop();
        }

        DebugIOWrite("Completed notification from channel %zu (%s) to %s",
                     i, type, addr.str().c_str());
    }

    COMMIT {
        m_lastNotified = i;
    }


    return SUCCESS;
}


void IONotificationMultiplexer::Cmd_Info(ostream& out, const vector<string>& /*args*/) const
{
    out << "I/O notification multiplexer." << endl
        << endl
        << "Number of channels: " << m_writebacks.size() << endl;
}

void IONotificationMultiplexer::Cmd_Read(ostream& out, const vector<string>& /*args*/) const
{
    out << "Channel | Enable | WB    | Int. Latch | Notifications" << endl
        << "--------+--------+-------+------------+---------------------" << endl;
    for (size_t i = 0; i < m_writebacks.size(); ++i)
    {
        out << setfill(' ') << setw(7) << i
            << " | "
            << setw(6) << setfill(' ')
            << (m_mask[i] ? "yes" : "no")
            << " | ";
        if (m_writebacks[i]->Empty())
        {
            out << "  -  ";
        }
        else
        {
            out << m_writebacks[i]->Read().str();
        }
        out << " | " << setw(10) << setfill(' ')
            << (m_interrupts[i]->IsSet() ? "set" : "unset")
            << " | ";
        if (m_notifications[i]->Empty())
        {
            out << "none";
        }
        else
        {
            for (Buffer<Integer>::const_iterator p = m_notifications[i]->begin(); p != m_notifications[i]->end(); ++p)
            {
                out << hex << *p << ' ';
            }
        }
        out << dec << endl;
    }
}

StorageTraceSet IONotificationMultiplexer::GetWriteBackTraces() const
{
    StorageTraceSet res;
    for (std::vector<Register<RegAddr>*>::const_iterator p = m_writebacks.begin(); p != m_writebacks.end(); ++p)
    {
        res ^= *(*p);
    }
    return res;
}


}
}
