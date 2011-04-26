#include "Processor.h"
#include "sim/config.h"
#include <sstream>

namespace Simulator
{

Processor::IOInterruptMultiplexer::IOInterruptMultiplexer(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, size_t numInterrupts)
    : Object(name, parent, clock),
      m_regFile(rf),
      m_lastNotified(0),
      p_IncomingInterrupts("received-interrupts", delegate::create<IOInterruptMultiplexer, &Processor::IOInterruptMultiplexer::DoReceivedInterrupts>(*this))
{
    m_writebacks.resize(numInterrupts, 0);
    m_interrupts.resize(numInterrupts, 0);
    for (size_t i = 0; i < numInterrupts; ++i)
    {
        {
            std::stringstream ss;
            ss << "int_wb" << i;
            m_writebacks[i] = new Register<RegAddr>(ss.str(), *this, clock);
            m_writebacks[i]->Sensitive(p_IncomingInterrupts);
        }
        {
            std::stringstream ss;
            ss << "int_latch" << i;
            m_interrupts[i] = new SingleFlag(ss.str(), *this, clock, false);
            m_interrupts[i]->Sensitive(p_IncomingInterrupts);
        }
    }

}

bool Processor::IOInterruptMultiplexer::SetWriteBackAddress(IOInterruptID which, const RegAddr& addr)
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

bool Processor::IOInterruptMultiplexer::OnInterruptRequestReceived(IOInterruptID from)
{
    assert(from < m_interrupts.size());
    
    return m_interrupts[from]->Set();
}

Result Processor::IOInterruptMultiplexer::DoReceivedInterrupts()
{
    size_t i;
    bool   found = false;

    /* Search for an interrupt to signal back to the processor */
    for (i = m_lastNotified; i < m_interrupts.size(); ++i)
    {
        if (m_interrupts[i]->IsSet() && !m_writebacks[i]->Empty())
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        for (i = 0; i < m_lastNotified; ++i)
        {
            if (m_interrupts[i]->IsSet() && !m_writebacks[i]->Empty())
            {
                found = true;
                break;
            }
        }
    }
    
    if (!found)
    {
        // Nothing to do
        return SUCCESS;
    }

    /* An interrupt was found, try to notify the processor */
    
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
        DeadlockWrite("I/O interrupt delivered before register %s was cleared", addr.str().c_str());
        return FAILED;
    }
    
    // Now write
    regvalue.m_state = RST_FULL;

    switch (addr.type) {
        case RT_INTEGER: regvalue.m_integer       = i; break;
        case RT_FLOAT:   regvalue.m_float.integer = i; break;
    }

    if (!m_regFile.WriteRegister(addr, regvalue, false))
    {
        DeadlockWrite("Unable to write register %s", addr.str().c_str());
        return FAILED;
    }
    
    DebugIOWrite("Completed notification of interrupt %zu to %s",
                 i, addr.str().c_str());

    m_writebacks[i]->Clear();
    m_interrupts[i]->Clear();
    m_lastNotified = i;

    return SUCCESS;
}

    
}
