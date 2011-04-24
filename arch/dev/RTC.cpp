#include "RTC.h"
#include <sys/time.h>
#include <csignal>
#include <cerrno>

using namespace std;

namespace Simulator
{

    /* the following implements a very crude semaphore.

       We want that several RTC clocks can update their time
       when a tick event is received. They could update more
       often but we want to limit syscall usage. 

       To do this we maintain a count of the RTC clocks
       that have acknowledged the tickevent in "timerTicked".
       The count is reset to the number of RTC clocks
       every time SIGALRM ticks; each RTC clock's event
       handler then decreases it. 
    */

    static volatile unsigned clockSemaphore = 0;
    static unsigned clockListeners = 0;

    static clock_delay_t clockResolution = 0;
    static volatile precise_time_t currentTime = 0;

    static void alarm_handler(int)
    {
        clockSemaphore = clockListeners;
        struct timeval tv;
        int gtod_status = gettimeofday(&tv, NULL);
        assert(gtod_status == 0);
        precise_time_t newTime = tv.tv_usec + tv.tv_sec * 1000000;
        currentTime = newTime;
    }

    static void setup_clocks(const Config& config)
    {
        static bool initialized = false;
        if (!initialized)
        {
            // update delay in microseconds
            clockResolution = config.getValue<clock_delay_t>("RTCMeatSpaceUpdateInterval", 20000);

            if (SIG_ERR == signal(SIGALRM, alarm_handler))
            {
                throw exceptf<SimulationException>("Cannot set alarm: %s", strerror(errno));
            };

            struct itimerval it;
            it.it_interval.tv_sec = clockResolution / 1000000;
            it.it_interval.tv_usec = clockResolution % 1000000;
            it.it_value = it.it_interval;

            if (-1 == setitimer(ITIMER_REAL, &it, NULL))
            {
                throw exceptf<SimulationException>("Cannot set timer: %s", strerror(errno));
            };

            initialized = true;
        }
    }

    RTC::RTC(const string& name, Object& parent, Clock& clock, IIOBus& iobus, IODeviceID devid, const Config& config)
        : Object(name, parent, clock),
          m_devid(devid),
          m_iobus(iobus),
          m_timerTicked(false),
          m_interruptNumber(0),
          m_timeOfLastInterrupt(0),
          m_triggerDelay(0),
          m_deliverAllEvents(false),
          p_checkTime("clock-update", delegate::create<RTC, &RTC::DoCheckTime>(*this))
    {
        setup_clocks(config);
    }

    Result RTC::DoCheckTime()
    {
        if (!m_timerTicked && (clockSemaphore != 0))
        {
            m_timerTicked = true;
            --clockSemaphore;
        }
        
        if (m_timerTicked)
        {
            if (m_triggerDelay != 0)
            {
                // The clock is configured to deliver interrupts. Check
                // for this.
                if (m_timeOfLastInterrupt + m_triggerDelay <= currentTime)
                {
                    // Time for an interrupt.
                    if (!m_iobus.SendInterruptRequest(m_devid, m_interruptNumber))
                    {
                        DeadlockWrite("Cannot send timer interrupt to I/O bus");
                        return FAILED;
                    }

                    COMMIT {
                        if (m_deliverAllEvents)
                        {
                            m_timeOfLastInterrupt += m_triggerDelay;
                        }
                        else
                        {
                            m_timeOfLastInterrupt = currentTime;
                        }
                    }
                }
            }
            COMMIT { 
                m_timerTicked = false; 
            }
        }
        return SUCCESS;
    }

    void RTC::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "RTC", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }    
    }


    static uint32_t pack_time(const struct tm* tm, bool otherbits)
    {
        if (!otherbits)
        {
            //       bits 0-5: seconds (0-59)
            //       bits 6-11: minutes (0-59)
            //       bits 12-16: hours (0-23)
            //       bits 17-21: day in month (0-30)
            //       bits 22-25: month in year (0-11)
            //       bits 26-31: year from 1970
            return (uint32_t)tm->tm_sec 
                | ((uint32_t)tm->tm_min << 6)
                | ((uint32_t)tm->tm_hour << 12)
                | ((uint32_t)tm->tm_mday << 17)
                | ((uint32_t)tm->tm_mon << 22)
                | ((uint32_t)(tm->tm_year + 70) << 26);
        }
        else
        {
            //       bits 0-3: day of week (sunday = 0)
            //       bits 4-12: day of year (0-365)
            //       bit 13: summer time in effect
            //       bits 14-31: offset from UTC in seconds  
            return (uint32_t)tm->tm_wday
                | ((uint32_t)tm->tm_yday << 4)
                | ((uint32_t)!!tm->tm_isdst << 13)
                | ((uint32_t)tm->tm_gmtoff << 14);
        }
    }

    bool RTC::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        unsigned word = address / 4;

        if (address % 4 != 0 || data.size != 4)
        {
            throw exceptf<SimulationException>(*this, "Invalid unaligned RTC write: %#016llx (%u)", (unsigned long long)address, (unsigned)data.size); 
        }
        if (word == 0 || word > 3)
        {
            throw exceptf<SimulationException>(*this, "Invalid write to RTC word: %u", word);
        }
        
        Integer value = UnserializeRegister(RT_INTEGER, data.data, data.size);
        
        COMMIT{
            switch(word)
            {
            case 1:   m_triggerDelay = value; break;
            case 2:   m_interruptNumber = value; break;
            case 3:   m_deliverAllEvents = value; break;
            }
        }
        return SUCCESS;
    }

    bool RTC::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        // the clock uses 32-bit control words 
        // word 0: resolution (microseconds)
        // word 1: interrupt delay (in microseconds, set to 0 to disable interrupts)
        // word 2: interrupt number to generate
        // word 3: whether to deliver all events
        // word 4: microseconds part of current time since jan 1, 1970
        // word 5: seconds part of current time since jan 1, 1970
        // word 6: packed UTC time: 
        // word 7: packed UTC time (more):
        // word 8,9: packed local time (same format as UTC)

        unsigned word = address / 4;
        uint32_t value = 0;

        if (address % 4 != 0 || size != 4)
        {
            throw exceptf<SimulationException>(*this, "Invalid unaligned RTC read: %#016llx (%u)", (unsigned long long)address, (unsigned)size); 
        }
        if (word > 9)
        {
            throw exceptf<SimulationException>(*this, "Read from invalid RTC address: %u", word);
        }

        COMMIT{
            switch(word)
            {
            case 0:   value = clockResolution; break;
            case 1:   value = m_triggerDelay; break;
            case 2:   value = m_interruptNumber; break;
            case 3:   value = (int)m_deliverAllEvents; break;
            case 4:   value = currentTime % 1000000; break;
            case 5:   value = currentTime / 1000000; break;
            case 6: case 7:
            {
                time_t c = time(0);
                struct tm * tm = gmtime(&c);
                value = pack_time(tm, word - 6);
                break;
            }
            case 8: case 9:
            {
                time_t c = time(0);
                struct tm * tm = localtime(&c);
                value = pack_time(tm, word - 8);
                break;
            }
            }
        }

        IOData iodata;
        SerializeRegister(RT_INTEGER, value, iodata.data, 4);
        iodata.size = 4;
        
        if (!m_iobus.SendReadResponse(m_devid, from, iodata))
        {
            DeadlockWrite("Cannot send RTC read response to I/O bus");
            return FAILED;
        }
        return SUCCESS;
    }


}
