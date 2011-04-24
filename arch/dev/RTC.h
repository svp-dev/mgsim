#ifndef RTC_H
#define RTC_H

#include "arch/IOBus.h"
#include "sim/kernel.h"
#include "sim/config.h"
#include <ctime>
#include <sys/time.h>

namespace Simulator
{

    typedef unsigned long long precise_time_t;
    typedef unsigned long      clock_delay_t;
       
    class RTC : public IIOBusClient, public Object
    {
        IODeviceID      m_devid;
        IIOBus&         m_iobus;
        bool            m_timerTicked;
        IOInterruptID   m_interruptNumber;

        precise_time_t  m_timeOfLastInterrupt;
        clock_delay_t   m_triggerDelay;
        bool            m_deliverAllEvents;

    public:

        RTC(const std::string& name, Object& parent, Clock& clock, IIOBus& iobus, IODeviceID devid, const Config& config);

        Process p_checkTime;

        Result DoCheckTime();

        bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
        bool OnReadResponseReceived(IODeviceID from, const IOData& data) { return false; }
        bool OnInterruptRequestReceived(IOInterruptID which) { return true; }
        bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
        void GetDeviceIdentity(IODeviceIdentification& id) const;

        std::string GetIODeviceName() const { return GetFQN(); }

    };


}

#endif
