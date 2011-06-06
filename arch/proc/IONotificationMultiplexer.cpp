#include "Processor.h"
#include "sim/config.h"
#include <sstream>

namespace Simulator
{

Processor::IONotificationMultiplexer::IONotificationMultiplexer(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, size_t numChannels, Config& config)
    : Object(name, parent, clock),
      m_regFile(rf),
      m_lastNotified(0),
      p_IncomingNotifications(*this, "received-notifications", delegate::create<IONotificationMultiplexer, &Processor::IONotificationMultiplexer::DoReceivedNotifications>(*this))
{
    m_writebacks.resize(numChannels, 0);
    m_interrupts.resize(numChannels, 0);
    m_notifications.resize(numChannels, 0);

    BufferSize nqs = config.getValue<BufferSize>(*this, "NotificationQueueSize");

    for (size_t i = 0; i < numChannels; ++i)
    {
        {
            std::stringstream ss;
            ss << "wb" << i;
            m_writebacks[i] = new Register<RegAddr>(ss.str(), *this, clock);
            m_writebacks[i]->Sensitive(p_IncomingNotifications);
        }
        {
            std::stringstream ss;
            ss << "latch" << i;
            m_interrupts[i] = new SingleFlag(ss.str(), *this, clock, false);
            m_interrupts[i]->Sensitive(p_IncomingNotifications);
        }
        {
            std::stringstream ss;
            ss << "b_notification" << i;
            m_notifications[i] = new Buffer<Integer>(ss.str(), *this, clock, nqs);
            m_notifications[i]->Sensitive(p_IncomingNotifications);
        }
    }

}

Processor::IONotificationMultiplexer::~IONotificationMultiplexer()
{
    for (size_t i = 0; i < m_writebacks.size(); ++i)
    {
        delete m_writebacks[i];
        delete m_interrupts[i];
        delete m_notifications[i];
    }
}

bool Processor::IONotificationMultiplexer::SetWriteBackAddress(IONotificationChannelID which, const RegAddr& addr)
{
    assert(which < m_writebacks.size());

    if (!m_writebacks[which]->Empty())
    {
        // some thread is already waiting, do not
        // allow another one to wait as well!
        return false;
    }

    m_writebacks[which]->Write(addr);
    return true;
}

bool Processor::IONotificationMultiplexer::OnInterruptRequestReceived(IONotificationChannelID from)
{
    assert(from < m_interrupts.size());
    
    return m_interrupts[from]->Set();
}

bool Processor::IONotificationMultiplexer::OnNotificationReceived(IONotificationChannelID from, Integer tag)
{
    assert(from < m_notifications.size());

    return m_notifications[from]->Push(tag);
}


Result Processor::IONotificationMultiplexer::DoReceivedNotifications()
{
    size_t i;
    bool   notification_ready = false;
    bool   pending_notifications = false;

    /* Search for a notification to signal back to the processor.
     The following two loops implement a circular lookup through
    all devices -- round robin delivery to ensure fairness. */
    for (i = m_lastNotified; i < m_interrupts.size(); ++i)
    {
        if (m_interrupts[i]->IsSet() || !m_notifications[i]->Empty())
        {
            pending_notifications = true;
            if (!m_writebacks[i]->Empty())
            {
                notification_ready = true;
                break;
            }
        }
    }
    if (!notification_ready)
    {
        for (i = 0; i < m_lastNotified; ++i)
        {
            if (m_interrupts[i]->IsSet() || !m_notifications[i]->Empty())
            {
                pending_notifications = true;
                if (!m_writebacks[i]->Empty())
                {
                    notification_ready = true;
                    break;
                }
            }
        }
    }
    
    if (!notification_ready)
    {
        if (pending_notifications)
        {
            DebugIOWrite("Some notification is ready but no active listener exists.");
            p_IncomingNotifications.Deactivate();
        }

        // Nothing to do
        return SUCCESS;
    }

    /* A notification was found, try to notify the processor */
    
    const RegAddr& addr = m_writebacks[i]->Read();

    if (!m_regFile.p_asyncW.Write(addr))
    {
        DeadlockWrite("Unable to acquire port to write back %s", addr.str().c_str());
        return FAILED;
    }

    RegValue regvalue;
    if (!m_regFile.ReadRegister(addr, regvalue))
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
    regvalue.m_state = RST_FULL;
    Integer value;
    if (m_interrupts[i]->IsSet())
    {
        // Interrupts have priority over notifications
        value = 0;
    }
    else
    {
        value = m_notifications[i]->Front();
    }

    switch (addr.type) {
        case RT_INTEGER: regvalue.m_integer       = value; break;
        case RT_FLOAT:   regvalue.m_float.integer = value; break;
    }

    if (!m_regFile.WriteRegister(addr, regvalue, false))
    {
        DeadlockWrite("Unable to write register %s", addr.str().c_str());
        return FAILED;
    }
    
    DebugIOWrite("Completed notification of channel %zu to %s",
                 i, addr.str().c_str());

    m_writebacks[i]->Clear();

    if (m_interrupts[i]->IsSet())
    {
        m_interrupts[i]->Clear();
    }
    else
    {
        m_notifications[i]->Pop();
    }

    COMMIT {
        m_lastNotified = i;
    }

    return SUCCESS;
}

    
}
