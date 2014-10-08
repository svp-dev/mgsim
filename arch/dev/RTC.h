// -*- c++ -*-
#ifndef RTC_H
#define RTC_H

#include "arch/IOBus.h"
#include "sim/kernel.h"
#include "sim/config.h"
#include "sim/flag.h"

#include <ctime>
#include <sys/time.h>

namespace Simulator
{

    typedef unsigned long long precise_time_t;
    typedef unsigned long      clock_delay_t;

    class RTC : public Object
    {
        bool            m_timerTicked;

        DefineStateVariable(precise_time_t, timeOfLastInterrupt);
        DefineStateVariable(clock_delay_t, triggerDelay);
        DefineStateVariable(bool, deliverAllEvents);

        Flag            m_enableCheck;


        class RTCInterface : public IIOBusClient, public Object
        {
            IODeviceID      m_devid;
            IIOBus&         m_iobus;
            Flag            m_doNotify;
            DefineStateVariable(IONotificationChannelID, interruptNumber);

            RTC&            GetRTC() { return *dynamic_cast<RTC*>(GetParent()); }

        public:
            RTCInterface(const std::string& name, RTC& parent, IIOBus& iobus, IODeviceID devid);

            Process p_notifyTime;

            Result DoNotifyTime();

            friend class RTC;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);

            // Admin
            void Initialize();
            const std::string& GetIODeviceName() const;
            void GetDeviceIdentity(IODeviceIdentification& id) const;
        };

        RTCInterface         m_businterface;
        friend class RTCInterface;

    public:

        RTC(const std::string& name, Object& parent, Clock& clock, IIOBus& iobus, IODeviceID devid);

        Process p_checkTime;

        Result DoCheckTime();

    };


}

#endif
