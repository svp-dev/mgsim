#include "SMC.h"
#include "sim/config.h"
#include "arch/proc/Processor.h"
#include "ActiveROM.h"

using namespace std;

namespace Simulator
{
    SMC::SMC(const string& name, Object& parent, IIOBus& iobus, IODeviceID devid,
             const vector<pair<RegAddr, RegValue> >& regs,
             Processor& proc, 
             ActiveROM& rom, 
             Config& config)
        : Object(name, parent, iobus.GetClock()),
          m_enumdata(NULL),
          m_size(0),
          m_iobus(iobus),
          m_devid(devid),
          m_regs(regs),
          m_cpu(proc),
          m_rom(rom),
          m_romid(rom.GetDeviceID()),
          m_atboot(true),
          m_enable_dca(!rom.IsPreloaded()),
          m_start_dca("f_start_dca", *this, iobus.GetClock(), false),
          m_doboot("f_doboot", *this, iobus.GetClock(), false),
          p_StartDCA("start-dca", delegate::create<SMC, &SMC::DoStartDCA>(*this)),
          p_Boot("boot", delegate::create<SMC, &SMC::DoBoot>(*this))
    {

        assert(m_rom.IsBootable());

        iobus.RegisterClient(devid, *this);
        
        m_start_dca.Sensitive(p_StartDCA);
        m_doboot.Sensitive(p_Boot);
    }

    SMC::~SMC()
    {
        delete m_enumdata;
    }

    void SMC::Initialize(size_t numDevices)
    {
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

        if (m_enable_dca)
        {
            m_start_dca.Set();
        }
        else
        {
            m_doboot.Set();
        }
    }

    Result SMC::DoStartDCA()
    {
        IOData iodata;
        iodata.size = sizeof(Integer);
        memset(iodata.data, 0, sizeof(Integer));

        if (!m_iobus.SendWriteRequest(m_devid, m_romid, 0, iodata))
        {
            DeadlockWrite("Unable to send ROM DCA trigger request");
            return FAILED;
        }
        m_start_dca.Clear();

        DebugIOWrite("Sent ROM DCA trigger to device %u", (unsigned)m_romid);

        return SUCCESS;
    }

    Result SMC::DoBoot()
    {
        DebugIOWrite("Sending boot signal to processor %s", m_cpu.GetName().c_str());

        COMMIT {
            MemAddr prog_start;
            bool legacy;
            m_rom.GetBootInfo(prog_start, legacy);
            m_cpu.Boot(prog_start, legacy, m_cpu.GetGridSize(), m_cpu.GetDeviceBaseAddress(m_devid));

            // Fill initial registers
            for (size_t i = 0; i < m_regs.size(); ++i)
            {
                m_cpu.WriteRegister(m_regs[i].first, m_regs[i].second);
            }
            m_atboot = false;
        }
        m_doboot.Clear();
        return SUCCESS;
    }

    bool SMC::OnNotificationReceived(IOInterruptID which, Integer tag)
    {
        if (m_enable_dca)
        {
            COMMIT { m_enable_dca = false; }
            m_doboot.Set();

            DebugIOWrite("ROM DCA transfer finished, ready to boot processor %s", m_cpu.GetName().c_str());
        }
        return true;
    }

    bool SMC::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        if (address >= m_size || address + size >= m_size)
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

    std::string SMC::GetIODeviceName() const 
    { 
        return GetFQN(); 
    }


}
