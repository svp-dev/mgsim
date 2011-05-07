#include "ActiveROM.h"
#include "ELFLoader.h"
#include <iostream>
#include <fstream>

using namespace std;

namespace Simulator
{
    void ActiveROM::LoadConfig(const Config& config)
    {
        std::ostringstream os;
        config.dumpConfiguration(os, "", true);
        std::string data = os.str();
        
        size_t romsize = data.size() + 1;
        
        m_numLines = romsize / m_lineSize;
        m_numLines = (romsize % m_lineSize == 0) ? m_numLines : (m_numLines + 1);
        
        m_data = new char [m_numLines * m_lineSize];
        memcpy(m_data, data.c_str(), data.size());
        m_data[romsize-1] = '\0';
    }

    void ActiveROM::LoadFile(const std::string& fname)
    {
        std::ifstream is;
        is.open(fname.c_str(), ios::binary);
        
        if (!is.good())
        {
            throw exceptf<InvalidArgumentException>(*this, "Unable to open file: %s", fname.c_str());
        }
        
        // get length of file:
        is.seekg (0, ios::end);
        size_t length = is.tellg();
        is.seekg (0, ios::beg);
        
        if (!is.good())
        {
            throw exceptf<InvalidArgumentException>(*this, "Unable to get file size: %s", fname.c_str());
        }
        
        if (length == 0)
        {
            throw exceptf<InvalidArgumentException>(*this, "File is empty: %s", fname.c_str());
        }
        
        m_numLines = length / m_lineSize;
        m_numLines = (length % m_lineSize == 0) ? m_numLines : (m_numLines + 1);
        
        m_data = new char [m_numLines * m_lineSize];
        
        is.read(m_data, length);
        
        if (!is.good())
        {
            throw exceptf<InvalidArgumentException>(*this, "Unable to read file: %s", fname.c_str());
        }
        
        is.close();
    }

    void ActiveROM::PrepareRanges(bool load_to_memory)
    {
        for (size_t i = 0; i < m_loadable.size(); ++i)
        {
            const LoadableRange& r = m_loadable[i];
            m_memory.Reserve(r.vaddr, r.size, r.perm | IMemory::PERM_DCA_WRITE);
            if (load_to_memory)
            {
                m_memory.Write(r.vaddr, m_data + r.rom_offset, r.size);
            }
        }
    }

    ActiveROM::ActiveROM(const std::string& name, Object& parent, IMemoryAdmin& mem, IIOBus& iobus, IODeviceID devid, Config& config)
        : Object(name, parent, iobus.GetClock()),
          m_memory(mem),
          m_data(NULL),
          m_lineSize(config.getValue<size_t>(*this, "ROMLineSize", config.getValue<size_t>("CacheLineSize"))),
          m_numLines(0),
          m_bootable(false),
          m_start_address(0),
          m_legacy(false),
          m_devid(devid),
          m_iobus(iobus),
          m_client(config.getValue<IODeviceID>(*this, "DCATargetID")),
          m_completionTarget(config.getValue<IOInterruptID>(*this, "DCANotificationChannel")),
          m_loading("f_loading", *this, iobus.GetClock(), false),
          m_notifying("f_notifying", *this, iobus.GetClock(), false),
          m_currentRange(0),
          m_currentOffset(0),
          p_Load("load", delegate::create<ActiveROM, &ActiveROM::DoLoad>(*this)),
          p_Notify("notify", delegate::create<ActiveROM, &ActiveROM::DoNotify>(*this))
    {
        iobus.RegisterClient(devid, *this);
        m_loading.Sensitive(p_Load);
        m_notifying.Sensitive(p_Notify);
        
        if (m_lineSize == 0)
        {
            throw exceptf<InvalidArgumentException>(*this, "ROMLineSize cannot be zero");
        }

        std::string source = config.getValue<std::string>(*this, "ROMContentSource");
        if (source == "RAW" || source == "ELF")
        {
            m_filename = config.getValue<std::string>(*this, "ROMFileName");
            LoadFile(m_filename);
        }
        else if (source == "CONFIG")
        {
            LoadConfig(config);
        }
        else
        {
            throw exceptf<InvalidArgumentException>(*this, "Unrecognized ROMContentSource: %s", source.c_str());
        }

        if (source == "CONFIG" || source == "RAW")
        {
            LoadableRange r;
            r.rom_offset = 0;
            r.vaddr = config.getValue<MemAddr>(*this, "ROMBaseAddr");
            r.size = m_numLines * m_lineSize;
            r.perm = IMemory::PERM_READ;

            m_loadable.push_back(r);
        }
        else if (source == "ELF")
        {
            bool verbose = config.getValue<bool>(*this, "VerboseELFLoad");
            std::pair<MemAddr, bool> res = LoadProgram(m_loadable, m_memory, m_data, m_numLines * m_lineSize, verbose);
            m_bootable = true;
            m_start_address = res.first;
            m_legacy = res.second;
        }

        bool preload = config.getValue<bool>(*this, "PreloadROMToRAM");
        PrepareRanges(preload);
    }

    void ActiveROM::GetBootInfo(MemAddr& start, bool& legacy) const
    {
        start = m_start_address;
        legacy = m_legacy;
    }

    Result ActiveROM::DoNotify()
    {
        if (!m_iobus.SendNotification(m_devid, m_completionTarget, m_devid))
        {
            DeadlockWrite("Unable to send DCA completion notification");
            return FAILED;
        }
        m_notifying.Clear();
        return SUCCESS;
    }
    
    Result ActiveROM::DoLoad()
    {      
        LoadableRange& r = m_loadable[m_currentRange];
        size_t offset = r.rom_offset + m_currentOffset;
        size_t voffset = r.vaddr + m_currentOffset;
        
        IOData data;
        data.size = min((MemSize)(r.size - m_currentOffset), (MemSize)m_lineSize);
        memcpy(data.data, m_data + offset, data.size);

        if (!m_iobus.SendWriteRequest(m_devid, m_client, voffset, data))
        {
            DeadlockWrite("Unable to send DCA write for ROM data %#016llx/%u to device %u", 
                          (unsigned long long)(voffset), (unsigned)data.size, (unsigned)m_client);
            return FAILED;
        }

        if (m_currentOffset + m_lineSize < r.size)
        {
            COMMIT {
                m_currentOffset += m_lineSize;
            }
        }
        else if (m_currentOffset + m_lineSize >= r.size && m_currentRange + 1 < m_loadable.size())
        {
            COMMIT {
                ++m_currentRange;
                m_currentOffset = 0;
            }
        }
        else
        {
            m_notifying.Set();
            m_loading.Clear();
        }
        return SUCCESS;
    }


    ActiveROM::~ActiveROM()
    {
        delete m_data;
    }

    bool ActiveROM::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        if (address >= m_lineSize * m_numLines)
        {
            throw exceptf<SimulationException>(*this, "Invalid I/O read to %#016llx/%u", (unsigned long long)address, (unsigned)size);
        }

        IOData iodata;
        iodata.size = size;
        memcpy(iodata.data, m_data, size);
        if (!m_iobus.SendReadResponse(m_devid, from, address, iodata))
        {
            DeadlockWrite("Unable to send ROM read response to I/O bus");
            return false;
        }
        return true;
    }

    bool ActiveROM::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        if (address != 0)
        {
            throw exceptf<SimulationException>(*this, "Invalid I/O read to %#016llx/%u", (unsigned long long)address, (unsigned)data.size);
        }
        m_notifying.Clear();
        m_loading.Set();
    }

    void ActiveROM::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "ActiveROM", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }    
    }

    std::string ActiveROM::GetIODeviceName() const 
    { 
        return GetFQN(); 
    }

}
