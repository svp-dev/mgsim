// -*- c++ -*-
#ifndef RTC_H
#define RTC_H

#include "arch/IOMessageInterface.h"
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


        class RTCInterface : public IIOMessageClient, public Object
        {
            IODeviceID          m_devid;
            IOMessageInterface& m_ioif;
            Flag                m_doNotify;
            DefineStateVariable(IONotificationChannelID, interruptNumber);

            RTC&            GetRTC() const { return *dynamic_cast<RTC*>(GetParent()); }

        public:
            RTCInterface(const std::string& name, RTC& parent,
                         IOMessageInterface& ioif, IODeviceID devid);

            Process p_notifyTime;

            Result DoNotifyTime();

            friend class RTC;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) override;
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data) override;
            StorageTraceSet GetReadRequestTraces() const override;
            StorageTraceSet GetWriteRequestTraces() const override;

            // Admin
            void Initialize() override;
            const std::string& GetIODeviceName() const override;
            void GetDeviceIdentity(IODeviceIdentification& id) const override;
        };

        RTCInterface         m_businterface;
        friend class RTCInterface;

    public:

        RTC(const std::string& name, Object& parent, Clock& clock,
            IOMessageInterface& ioif, IODeviceID devid);

        Process p_checkTime;

        Result DoCheckTime();

    };


}

#endif
