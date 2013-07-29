#include "SMC.h"
#include "sim/config.h"
#include "arch/drisc/DRISC.h"
#include "ActiveROM.h"

using namespace std;

namespace Simulator
{
    SMC::SMC(const string& name, Object& parent, IIOBus& iobus, IODeviceID devid, Config& config)
        : Object(name, parent, iobus.GetClock()),
          m_enumdata(NULL),
          m_size(0),
          m_iobus(iobus),
          m_devid(devid),
          m_config(config),
          m_regs(config.getWordList(*this, "InitRegs")),
          m_cpu(NULL),
          m_rom(NULL),
          m_romid(0),
          m_enable_boot(config.getValue<bool>(*this, "BootProcessor")),
          m_enable_dca(false),
          m_start_dca("f_start_dca", *this, iobus.GetClock(), false),
          m_doboot("f_doboot", *this, iobus.GetClock(), false),
          p_StartDCA(*this, "start-dca", delegate::create<SMC, &SMC::DoStartDCA>(*this)),
          p_Boot    (*this, "boot", delegate::create<SMC, &SMC::DoBoot>(*this))
    {
        iobus.RegisterClient(devid, *this);

        // Linked ROM

        m_rom = dynamic_cast<ActiveROM*>(& iobus.GetDeviceByName(config.getValue<string>(*this, "LinkedROM")));
        if (m_rom == NULL)
        {
            throw exceptf<InvalidArgumentException>(*this, "LinkedROM does not name a ROM device");
        }

        if (!m_rom->IsPreloaded())
        {
            m_enable_dca = true;
        }

        m_romid = m_rom->GetDeviceID();

        // Linked processor

        DRISC::IOBusInterface *cpu_if = dynamic_cast<DRISC::IOBusInterface*>(& iobus.GetDeviceByName(config.getValue<string>(*this, "LinkedProcessor") + ".io_if.bus_if"));
        if (cpu_if == NULL)
        {
            throw exceptf<InvalidArgumentException>(*this, "LinkedProcessor does not name a processor");
        }

        m_cpu = & cpu_if->GetDRISC();

        // component processes

        m_start_dca.Sensitive(p_StartDCA);
        m_doboot.Sensitive(p_Boot);
    }

    SMC::~SMC()
    {
        delete[] m_enumdata;
    }

    void SMC::Initialize()
    {
        if (m_enable_boot && !m_rom->IsBootable())
        {
            throw exceptf<InvalidArgumentException>(*this, "LinkedROM is not bootable: %s", m_rom->GetFQN().c_str());
        }

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

        m_cpu->GetIOInterface()->Initialize(m_devid);

        if (m_enable_dca)
        {
            m_start_dca.Set();
        }
        else if (m_enable_boot)
        {
            m_doboot.Set();
        }

        p_StartDCA.SetStorageTraces(m_iobus.GetWriteRequestTraces());

        p_Boot.SetStorageTraces(StorageTrace());
    }

    Result SMC::DoStartDCA()
    {
        IOData iodata;
        iodata.size = 4;
        memset(iodata.data, 0, 4);

        if (!m_iobus.SendWriteRequest(m_devid, m_romid, 0, iodata))
        {
            DeadlockWrite("Unable to send ROM DCA trigger request");
            return FAILED;
        }
        m_start_dca.Clear();

        DebugIOWrite("Sent ROM DCA trigger to device %u", (unsigned)m_romid);

        return SUCCESS;
    }

    void SMC::InitRegs() const
    {
        for (auto ri : m_regs)
        {
            // format is RNNN=VAL or FNNN=VAL
            transform(ri.begin(), ri.end(), ri.begin(), ::tolower);
            if (ri[0] != 'r' && ri[0] != 'f')
            {
                throw exceptf<InvalidArgumentException>("Register name not recognized: %s", ri.c_str());
            }
            // find "=" sign
            size_t i = ri.find('=');
            if (i < 2 || i + 1 > ri.size()) // need at least 2 chars before and 1 char after
            {
                throw exceptf<InvalidArgumentException>("Invalid register specifier: %s", ri.c_str());
            }
            string sidx = ri.substr(1, i - 1), value = ri.substr(i + 1);

            char* endptr;
            unsigned long idx = strtoul(sidx.c_str(), &endptr, 0);
            if (*endptr != '\0') 
            {
                throw exceptf<InvalidArgumentException>("Invalid register number: %s (%s)", sidx.c_str(), ri.c_str());
            }

            RegAddr reg_addr = MAKE_REGADDR((ri[0] == 'r') ? RT_INTEGER : RT_FLOAT, idx);
            RegValue reg_value;
            reg_value.m_state = RST_FULL;

            assert(value.size() > 0); // because of check above

            // First handle value indirections
            if (value[0] == '$')
            {
                value = m_config.getValue<string>(value.substr(1));
            }
                
            // Then handle actual value conversion, after indirection            
            if (value[0] == 'b')
            {
                reg_value.m_integer = m_cpu->GetDeviceBaseAddress(m_iobus.GetDeviceIDByName(value.substr(1)));
            }
            else if (value[0] == 'i' || (ri[0] == 'r' && (value[0] == '-' || (value[0] >= '0' && value[0] <= '9'))))
            {
                string nval = (value[0] == 'i') ? value.substr(1) : value;
                reg_value.m_integer = strtoull(nval.c_str(), &endptr, 0);
                if (*endptr != '\0')
                {
                    throw exceptf<InvalidArgumentException>("Invalid register value: %s (%s)", nval.c_str(), ri.c_str());
                }
            }
            else if (value[0] == 'f' || (ri[0] == 'f' && (value[0] == '-' || (value[0] >= '0' && value[0] <= '9'))))
            {
                string nval = (value[0] == 'f') ? value.substr(1) : value;
                reg_value.m_float.fromfloat(strtod(nval.c_str(), &endptr));
                if (*endptr != '\0')
                {
                    throw exceptf<InvalidArgumentException>("Invalid register value: %s (%s)", nval.c_str(), ri.c_str());
                }
            }
            else
            {
                throw exceptf<InvalidArgumentException>("Invalid register initializer: %s", ri.c_str());
            }

            m_cpu->WriteRegister(reg_addr, reg_value);

        }
    }

    Result SMC::DoBoot()
    {
        DebugIOWrite("Sending boot signal to processor %s", m_cpu->GetName().c_str());

        COMMIT {
            MemAddr prog_start;
            bool legacy;
            m_rom->GetBootInfo(prog_start, legacy);
            m_cpu->Boot(prog_start, legacy, m_cpu->GetGridSize(), m_cpu->GetDeviceBaseAddress(m_devid));

            InitRegs();
        }
        m_doboot.Clear();
        return SUCCESS;
    }

    bool SMC::OnNotificationReceived(IONotificationChannelID /*which*/, Integer /*tag*/)
    {
        if (m_enable_dca)
        {
            COMMIT { m_enable_dca = false; }

            DebugIOWrite("ROM DCA transfer finished");

            if (m_enable_boot)
            {
                m_doboot.Set();
            }
        }
        return true;
    }

    StorageTraceSet SMC::GetNotificationTraces() const
    {
        return opt(m_doboot);
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

    std::string SMC::GetIODeviceName() const
    {
        return GetFQN();
    }


}
