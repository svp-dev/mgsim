#ifndef RTC_H
#define RTC_H

#include "arch/IOBus.h"
#include "sim/kernel.h"
#include "sim/config.h"
#include "sim/storage.h"

#include <ctime>
#include <sys/time.h>

namespace Simulator
{

    typedef unsigned long long precise_time_t;
    typedef unsigned long      clock_delay_t;
       
    class RTC : public Object
    {
        bool            m_timerTicked;

        precise_time_t  m_timeOfLastInterrupt;
        clock_delay_t   m_triggerDelay;
        bool            m_deliverAllEvents;

        SingleFlag      m_enableCheck;


        class RTCInterface : public IIOBusClient, public Object
        {
            IODeviceID      m_devid;
            IIOBus&         m_iobus;
            SingleFlag      m_doNotify;
            IOInterruptID   m_interruptNumber;

            RTC&            GetRTC() { return *dynamic_cast<RTC*>(GetParent()); }

        public:
            RTCInterface(const std::string& name, RTC& parent, IIOBus& iobus, IODeviceID devid);

            Process p_notifyTime;

            Result DoNotifyTime();

            friend class RTC;

            bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
            bool OnReadResponseReceived(IODeviceID from, const IOData& data) { return false; }
            bool OnInterruptRequestReceived(IOInterruptID which) { return true; }
            bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
            void GetDeviceIdentity(IODeviceIdentification& id) const;
            
            std::string GetIODeviceName() const { return GetFQN(); }
        };

        RTCInterface         m_businterface;
        friend class RTCInterface;
        
    public:

        RTC(const std::string& name, Object& parent, Clock& clock, IIOBus& iobus, IODeviceID devid, Config& config);

        Process p_checkTime;

        Result DoCheckTime();

    };


}

#endif
