// -*- c++ -*-
#ifndef SMC_H
#define SMC_H

#include <arch/IOBus.h>
#include <sim/kernel.h>

class Config;
namespace Simulator
{
    class SMC : public IIOBusClient, public Object
    {
        char *m_enumdata;
        size_t m_size;


        IIOBus& m_iobus;
        IODeviceID m_devid;

    public:
        SMC(const std::string& name, Object& parent, IIOBus& iobus, IODeviceID devid);
        SMC(const SMC&) = delete;
        SMC& operator=(const SMC&) = delete;
        ~SMC();

        void Initialize();

        bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);

        void GetDeviceIdentity(IODeviceIdentification& id) const;
        const std::string& GetIODeviceName() const;
    };


}


#endif
