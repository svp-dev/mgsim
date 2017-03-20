#include "SMC.h"
#include "sim/config.h"
#include "arch/drisc/DRISC.h"
#include "ActiveROM.h"
#include <cstring>

using namespace std;

namespace Simulator
{
    SMC::SMC(const string& name, Object& parent, IOMessageInterface& ioif, IODeviceID devid)
        : Object(name, parent),
          m_enumdata(NULL),
          m_size(0),
          m_ioif(ioif),
          m_devid(devid)
    {
        ioif.RegisterClient(devid, *this);
    }

    SMC::~SMC()
    {
        delete[] m_enumdata;
    }

    void SMC::Initialize()
    {
        size_t numDevices = m_ioif.GetLastDeviceID();
        m_size = (numDevices + 1) * 8;
        m_enumdata = new char[m_size];

        SerializeRegister(RT_INTEGER, numDevices, m_enumdata, 8);
        for (size_t i = 0; i < numDevices; ++i)
        {
            IODeviceIdentification id;
            m_ioif.GetDeviceIdentity(i, id);
            SerializeRegister(RT_INTEGER, id.provider, m_enumdata + (i + 1) * 8, 2);
            SerializeRegister(RT_INTEGER, id.model,    m_enumdata + (i + 1) * 8 + 2, 2);
            SerializeRegister(RT_INTEGER, id.revision, m_enumdata + (i + 1) * 8 + 4, 2);
        }
    }

    StorageTraceSet SMC::GetReadRequestTraces() const
    {
        return m_ioif.GetRequestTraces(m_devid);
    }

    bool SMC::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        if (address + size > m_size)
        {
            throw exceptf<>(*this, "Invalid I/O read to %#016llx/%u", (unsigned long long)address, (unsigned)size);
        }

        IOMessage *msg = m_ioif.CreateReadResponse(m_devid, address, size);
        COMMIT {
            memcpy(msg->read_response.data.data, m_enumdata + address, size);
        }
        if (!m_ioif.SendMessage(m_devid, from, msg))
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
