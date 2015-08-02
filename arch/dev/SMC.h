// -*- c++ -*-
#ifndef SMC_H
#define SMC_H

#include <arch/IOMessageInterface.h>
#include <sim/kernel.h>

class Config;
namespace Simulator
{
    class SMC : public IIOMessageClient, public Object
    {
        char *m_enumdata;
        size_t m_size;


        IOMessageInterface& m_ioif;
        IODeviceID m_devid;

    public:
        SMC(const std::string& name, Object& parent, IOMessageInterface& iobus, IODeviceID devid);
        SMC(const SMC&) = delete;
        SMC& operator=(const SMC&) = delete;
        ~SMC();

        void Initialize() override;

        bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size) override;
        StorageTraceSet GetReadRequestTraces() const override;

        void GetDeviceIdentity(IODeviceIdentification& id) const override;
        const std::string& GetIODeviceName() const override;
    };


}


#endif
