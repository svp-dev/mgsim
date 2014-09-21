#include "SMC.h"
#include "sim/config.h"
#include "arch/drisc/DRISC.h"
#include "ActiveROM.h"

using namespace std;

namespace Simulator
{
    SMC::SMC(const string& name, Object& parent, IIOBus& iobus, IODeviceID devid)
        : Object(name, parent),
          m_enumdata(NULL),
          m_size(0),
          m_iobus(iobus),
          m_devid(devid)
    {
        iobus.RegisterClient(devid, *this);
    }

    SMC::~SMC()
    {
        delete[] m_enumdata;
    }

    void SMC::Initialize()
    {
        size_t numDevices = m_iobus.GetLastDeviceID();
        m_size = (numDevices + 1) * 8;
        m_enumdata = new char[m_size];

        SerializeRegister(RT_INTEGER, numDevices, m_enumdata, 8);
        for (size_t i = 0; i < numDevices; ++i)
        {
            IODeviceIdentification id;
            m_iobus.GetDeviceIdentity(i, id);
            SerializeRegister(RT_INTEGER, id.provider, m_enumdata + (i + 1) * 8, 2);
            SerializeRegister(RT_INTEGER, id.model,    m_enumdata + (i + 1) * 8 + 2, 2);
            SerializeRegister(RT_INTEGER, id.revision, m_enumdata + (i + 1) * 8 + 4, 2);
        }
    }

    bool SMC::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        if (address + size > m_size)
        {
            throw exceptf<SimulationException>(*this, "Invalid I/O read to %#016llx/%u", (unsigned long long)address, (unsigned)size);
        }

        IOData iodata;
        iodata.size = size;
        memcpy(iodata.data, m_enumdata + address, size);
        if (!m_iobus.SendReadResponse(m_devid, from, address, iodata))
        {
            DeadlockWrite("Unable to send SMC read response to I/O bus");
            return false;
        }
        return true;
    }

    void SMC::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "SMC", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    const std::string& SMC::GetIODeviceName() const
    {
        return GetName();
    }


}
