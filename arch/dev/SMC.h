#ifndef SMC_H
#define SMC_H

#include <arch/IOBus.h>
#include <sim/storage.h>

class Config;
namespace Simulator
{
    class Processor;
    class ActiveROM;

    class SMC : public IIOBusClient, public Object
    {
    private:
        char *m_enumdata;
        size_t m_size;


        IIOBus& m_iobus;
        IODeviceID m_devid;

        Config& m_config;
        const std::vector<std::string> m_regs;
        Processor* m_cpu;

        ActiveROM* m_rom;
        IODeviceID m_romid;

        bool       m_enable_boot;   // whether to boot the linked processor
        bool       m_enable_dca;    // whether to do the initial DCA
        SingleFlag m_start_dca;
        SingleFlag m_doboot;

        void InitRegs() const;

    public:
        SMC(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID devid, Config& config);
        SMC(const SMC&) = delete;
        SMC& operator=(const SMC&) = delete;
        ~SMC();

        void Initialize();

        Process p_StartDCA;
        Process p_Boot;

        Result DoStartDCA();
        Result DoBoot();

        bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
        bool OnNotificationReceived(IONotificationChannelID which, Integer tag);

        StorageTraceSet GetNotificationTraces() const;

        void GetDeviceIdentity(IODeviceIdentification& id) const;
        std::string GetIODeviceName() const;


    };


}


#endif
