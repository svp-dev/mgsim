#include "ActiveROM.h"
#include "ELFLoader.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstring>

using namespace std;

namespace Simulator
{
    void ActiveROM::LoadConfig()
    {
        vector<uint32_t> db = GetKernel()->GetConfig()->GetConfWords();
        size_t romsize = db.size() * sizeof(uint32_t) + m_lineSize;

        m_numLines = romsize / m_lineSize;
        m_numLines = (romsize % m_lineSize == 0) ? m_numLines : (m_numLines + 1);

        m_data = new char [m_numLines * m_lineSize];
        for (size_t i = 0; i < db.size(); ++i)
        {
            SerializeRegister(RT_INTEGER, db[i], m_data + i * sizeof(uint32_t), sizeof(uint32_t));
        }

        if (m_verboseload)
        {
            clog << GetName() << ": configuration data: " << dec << romsize << " bytes generated" << endl;
        }
    }

    void ActiveROM::LoadArgumentVector()
    {
        vector<char> argdata;
        const vector<string>& argv = GetKernel()->GetConfig()->GetArgumentVector();
        for (auto& arg : argv)
        {
            argdata.insert(argdata.end(), arg.begin(), arg.end());
            argdata.push_back('\0');
        }
        argdata.push_back('\0');

        size_t romsize = argdata.size() + 3 * sizeof(uint32_t) + m_lineSize;

        m_numLines = romsize / m_lineSize;
        m_numLines = (romsize % m_lineSize == 0) ? m_numLines : (m_numLines + 1);

        m_data = new char [m_numLines * m_lineSize];

        SerializeRegister(RT_INTEGER, 0x56475241, m_data, sizeof(uint32_t));
        SerializeRegister(RT_INTEGER, argv.size(), m_data + 1 * sizeof(uint32_t), sizeof(uint32_t));
        SerializeRegister(RT_INTEGER, argdata.size(), m_data + 2 * sizeof(uint32_t), sizeof(uint32_t));
        memcpy(m_data + 3 * sizeof(uint32_t), &argdata[0], argdata.size());

        if (m_verboseload)
        {
            clog << GetName() << ": argv data: " << dec << romsize << " bytes generated" << endl;
        }
    }

    void ActiveROM::LoadFile(const string& fname)
    {
        ifstream is;
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

        if (m_verboseload)
        {
            clog << GetName() << ": loaded " << dec << length << " bytes from " << fname << endl;
        }


    }

    void ActiveROM::PrepareRanges()
    {
        for (auto& r : m_loadable)
        {
            m_memory.Reserve(r.vaddr, r.vsize, 0, r.perm | IMemory::PERM_DCA_WRITE);
            if (m_verboseload)
            {
                clog << GetName() << ": reserved " << dec << r.vsize << " bytes in main memory at 0x"
                     << hex << setfill('0') << setw(16) << r.vaddr
                     << " - 0x"
                     << hex << setfill('0') << setw(16) << r.vaddr + r.vsize - 1 ;
            }
            if (m_preloaded_at_boot)
            {
                m_memory.Write(r.vaddr, m_data + r.rom_offset, 0, r.rom_size);
                if (m_verboseload)
                {
                    clog << ", preloaded " << dec << r.rom_size << " bytes to DRAM from ROM offset 0x" << hex << r.rom_offset;
                }
            }
            if (m_verboseload)
            {
                clog << endl;
            }
        }
    }

    ActiveROM::ActiveROM(const string& name, Object& parent, IMemoryAdmin& mem, IIOBus& iobus, IODeviceID devid, bool quiet)
        : Object(name, parent),
          m_memory(mem),
          m_data(NULL),
          m_lineSize(GetConfOpt("ROMLineSize", size_t, GetTopConf("CacheLineSize", size_t))),
          m_numLines(0),
          m_loadable(),
          m_filename(),
          m_verboseload(!quiet),
          m_bootable(false),
          m_start_address(0),
          m_legacy(false),
          InitStateVariable(booting, false),
          m_preloaded_at_boot(GetConf("PreloadROMToRAM", bool)),
          m_devid(devid),
          m_iobus(iobus),
          InitStateVariable(client, GetConf("DCATargetID", IODeviceID)),
          InitStateVariable(completionTarget,
                            GetConf("DCANotificationChannel",
                                    IONotificationChannelID)),
          InitStorage(m_loading, iobus.GetClock(), false),
          InitStorage(m_flushing, iobus.GetClock(), false),
          InitStorage(m_notifying, iobus.GetClock(), false),
          InitStateVariable(currentRange, 0),
          InitStateVariable(currentOffset, 0),
          InitProcess(p_Load, DoLoad),
          InitProcess(p_Flush, DoFlush),
          InitProcess(p_Notify, DoNotify)
    {
        iobus.RegisterClient(devid, *this);
        m_loading.Sensitive(p_Load);
        m_notifying.Sensitive(p_Notify);
        m_flushing.Sensitive(p_Flush);

        if (m_lineSize == 0)
        {
            throw exceptf<InvalidArgumentException>(*this, "ROMLineSize cannot be zero");
        }
    }

    void ActiveROM::Initialize()
    {
        string source = GetConf("ROMContentSource", string);

        if (source == "RAW" || source == "ELF")
        {
            auto &v = GetKernel()->GetConfig()->GetArgumentVector();
            if (!v.empty())
            {
                m_filename = GetConfOpt("ROMFileName", string, v[0]);
            }
            else
            {
                m_filename = GetConf("ROMFileName", string);
            }
            LoadFile(m_filename);
        }
        else if (source == "CONFIG")
        {
            LoadConfig();
        }
        else if (source == "ARGV")
        {
            LoadArgumentVector();
        }
        else
        {
            throw exceptf<InvalidArgumentException>(*this, "Unrecognized ROMContentSource: %s", source.c_str());
        }

        if (source == "ELF")
        {
            pair<MemAddr, bool> res = LoadProgram(GetName() + ':' + m_filename, m_loadable, m_memory, m_data, m_numLines * m_lineSize, m_verboseload);
            m_bootable = true;
            m_start_address = res.first;
            m_legacy = res.second;
        }
        else /* not ELF */
        {
            MemAddr addr = GetConfOpt("ROMBaseAddr", MemAddr, 0);
            if (addr != 0)
            {
                LoadableRange r;
                r.vaddr = addr;
                r.rom_offset = 0;
                r.vsize = r.rom_size = m_numLines * m_lineSize;
                r.perm = (IMemory::Permissions)(IMemory::PERM_READ | IMemory::PERM_DCA_READ);

                m_loadable.push_back(r);
            }
        }

        PrepareRanges();

        p_Load.SetStorageTraces(m_iobus.GetWriteRequestTraces() * opt(m_flushing));
        p_Flush.SetStorageTraces(m_iobus.GetReadRequestTraces(m_devid));
        p_Notify.SetStorageTraces(m_iobus.GetNotificationTraces() ^ m_iobus.GetActiveMessageTraces());

        if (m_bootable)
        {
            if (m_preloaded_at_boot)
            {
                // Simply signal a spontaneous completion to target
                m_notifying.Set();
            }
            else
            {
                // Not loaded yet; first load, then completion will signal boot.
                m_loading.Set();
            }
            m_booting = true;
        }
    }


    Result ActiveROM::DoNotify()
    {
        if (m_booting)
        {
            if (!m_iobus.SendActiveMessage(m_devid, m_completionTarget, m_start_address, m_legacy))
            {
                DeadlockWrite("Unable to send boot active message");
                return FAILED;
            }
            COMMIT { m_booting = false; }
        }
        else if (!m_iobus.SendNotification(m_devid, m_completionTarget, m_devid))
        {
            DeadlockWrite("Unable to send DCA completion notification");
            return FAILED;
        }
        m_notifying.Clear();
        return SUCCESS;
    }

    bool ActiveROM::OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& /*data*/)
    {
        assert(from == m_client && address == 0);
        m_notifying.Set();
        return true;
    }

    StorageTraceSet ActiveROM::GetReadResponseTraces() const
    {
        return m_notifying;
    }

    Result ActiveROM::DoFlush()
    {
        if (!m_iobus.SendReadRequest(m_devid, m_client, 0, 0))
        {
            DeadlockWrite("Unable to send DCA flush request");
            return FAILED;
        }
        m_flushing.Clear();
        return SUCCESS;
    }

    Result ActiveROM::DoLoad()
    {
        LoadableRange& r = m_loadable[m_currentRange];
        size_t offset = r.rom_offset + m_currentOffset;
        MemAddr voffset = r.vaddr + m_currentOffset;

        // transfer size:
        // - cannot be greater than the line size
        // - cannot be greated than the number of bytes remaining on the ROM
        // - cannot cause the range [voffset + size] to cross over a line boundary.
        MemSize transfer_size = min(min((MemSize)(r.rom_size - m_currentOffset), (MemSize)m_lineSize),
                                    (MemSize)(m_lineSize - voffset % m_lineSize));

        IOData data;
        data.size = transfer_size;
        memcpy(data.data, m_data + offset, data.size);

        if (!m_iobus.SendWriteRequest(m_devid, m_client, voffset, data))
        {
            DeadlockWrite("Unable to send DCA write for ROM data %#016llx/%u to device %u",
                          (unsigned long long)(voffset), (unsigned)transfer_size, (unsigned)m_client);
            return FAILED;
        }

        if (m_currentOffset + transfer_size < r.rom_size)
        {
            COMMIT {
                m_currentOffset += transfer_size;
            }
        }
        else if (m_currentOffset + transfer_size >= r.rom_size && m_currentRange + 1 < m_loadable.size())
        {
            COMMIT {
                ++m_currentRange;
                m_currentOffset = 0;
            }
        }
        else
        {
            m_flushing.Set();
            m_loading.Clear();

            COMMIT{
                m_currentRange = 0;
                m_currentOffset = 0;
            }
        }
        return SUCCESS;
    }


    ActiveROM::~ActiveROM()
    {
        delete[] m_data;
    }

    bool ActiveROM::OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size)
    {
        if (address >= m_lineSize * m_numLines || address + size > m_lineSize * m_numLines)
        {
            throw exceptf<>(*this, "Invalid I/O read to %#016llx/%u", (unsigned long long)address, (unsigned)size);
        }

        IOData iodata;
        iodata.size = size;
        memcpy(iodata.data, m_data + address, size);
        if (!m_iobus.SendReadResponse(m_devid, from, address, iodata))
        {
            DeadlockWrite("Unable to send ROM read response to I/O bus");
            return false;
        }
        return true;
    }

    bool ActiveROM::OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data)
    {
        if (address % 4 != 0 || address > 32 || data.size != 4)
        {
            throw exceptf<>(*this, "Invalid I/O write from device %u to %#016llx/%u", (unsigned)from, (unsigned long long)address, (unsigned)data.size);
        }

        Integer value = UnserializeRegister(RT_INTEGER, data.data, data.size);

        unsigned word = address / 4;

        if (word >= 2)
        {
            // ensure there is at least one loadable range
            if (m_loadable.empty())
            {
                COMMIT{
                    LoadableRange r;
                    r.vaddr = 0;
                    r.rom_offset = 0;
                    r.vsize = m_numLines * m_lineSize;
                    m_loadable.push_back(r);
                }
            }
        }

        switch(word)
        {
        case 0:
            m_notifying.Clear();
            m_flushing.Clear();
            m_loading.Set();
            break;
        case 1:
            COMMIT {
                m_client = (value & 0xffff);
                m_completionTarget = (value >> 16) & 0xffff;
            }
            break;
        case 2:
            COMMIT { m_loadable[0].rom_offset = (m_loadable[0].rom_offset & 0xffffffff00000000ULL) | (MemSize)value; }
            break;
        case 3:
#if MEMSIZE_WIDTH > 32
            COMMIT { m_loadable[0].rom_offset = (m_loadable[0].rom_offset & 0xffffffffULL) | ((MemSize)value << 32); }
#endif
            break;
        case 4:
            COMMIT { m_loadable[0].vaddr = (m_loadable[0].vaddr & 0xffffffff00000000ULL) | (MemSize)value; }
            break;
        case 5:
#if MEMSIZE_WIDTH > 32
            COMMIT { m_loadable[0].vaddr = (m_loadable[0].vaddr & 0xffffffffULL) | ((MemSize)value << 32); }
#endif
            break;
        case 6:
            COMMIT { m_loadable[0].rom_size = (m_loadable[0].rom_size & 0xffffffff00000000ULL) | (MemSize)value; }
            break;
        case 7:
#if MEMSIZE_WIDTH > 32
            COMMIT { m_loadable[0].rom_size = (m_loadable[0].rom_size & 0xffffffffULL) | ((MemSize)value << 32); }
#endif
            break;
        }
        return true;
    }

    StorageTraceSet ActiveROM::GetWriteRequestTraces() const
    {
        return opt(m_loading);
    }

    void ActiveROM::GetDeviceIdentity(IODeviceIdentification& id) const
    {
        if (!DeviceDatabase::GetDatabase().FindDeviceByName("MGSim", "ActiveROM", id))
        {
            throw InvalidArgumentException(*this, "Device identity not registered");
        }
    }

    const string& ActiveROM::GetIODeviceName() const
    {
        return GetName();
    }

    void ActiveROM::Cmd_Info(ostream& out, const vector<string>& /* args */) const
    {
        out << "The Active ROM is a combination of a read-only memory and a DMA controller." << endl
            << endl
            << "ROM size: " << m_numLines * m_lineSize << " bytes in " << m_numLines << " lines." << endl
            << "Bootable: " << (m_bootable ? "yes" : "no") << endl
            << "Target device for DCA: " << m_client << endl
            << "Notification channel for DCA completions: " << m_completionTarget << endl
            << "Preloaded to DRAM: " << (m_preloaded_at_boot ? "yes" : "no") << endl;
        if (!m_loadable.empty())
        {
            out << "Virtual ranges:" << endl
                << "ROM start | Bytes    | Virtual start    | Virtual end" << endl
                << "----------+----------+------------------+-----------------" << endl;
            for (auto& r : m_loadable)
            {
                out << setfill('0') << hex << setw(8) << r.rom_offset
                    << "  | "
                    << dec << setfill(' ') << setw(8) << r.rom_size
                    << " | "
                    << hex << setfill('0') << hex << setw(16) << r.vaddr
                    << " | "
                    << hex << setfill('0') << hex << setw(16) << r.vaddr + r.vsize - 1
                    << endl;
            }
        }
        else
        {
            out << "No loadable DRAM ranges." << endl;
        }
    }

    void ActiveROM::Cmd_Read(ostream& out, const vector<string>& arguments) const
    {
        MemAddr addr = 0;
        MemSize size = 0;
        char* endptr = NULL;

        if (arguments.size() == 2)
        {
            addr = (MemAddr)strtoull( arguments[0].c_str(), &endptr, 0 );
            if (*endptr == '\0')
            {
                size = strtoul( arguments[1].c_str(), &endptr, 0 );
            }
        }

        if (arguments.size() != 2 || *endptr != '\0')
        {
            out << "Usage: read <mem> <address> <count>" << endl;
            return;
        }

        if (addr + size > m_numLines * m_lineSize)
        {
            out << "Read past ROM boundary" << endl;
            return;
        }

        static const unsigned int BYTES_PER_LINE = 16;

        // Calculate aligned start and end addresses
        MemAddr start = addr / BYTES_PER_LINE * BYTES_PER_LINE;
        MemAddr end   = (addr + size + BYTES_PER_LINE - 1) / BYTES_PER_LINE * BYTES_PER_LINE;


        // Read the data
        vector<uint8_t> buf((size_t)size);
        memcpy(&buf[0], m_data + addr, size);

        // Print it
        for (MemAddr y = start; y < end; y += BYTES_PER_LINE)
        {
            // The address
            out << setw(8) << hex << setfill('0') << y << " | ";

            // The bytes
            for (MemAddr x = y; x < y + BYTES_PER_LINE; ++x)
            {
                if (x >= addr && x < addr + size)
                    out << setw(2) << (unsigned int)buf[(size_t)(x - addr)];
                else
                    out << "  ";

                // Print some space at half the grid
                if ((x - y) == BYTES_PER_LINE / 2 - 1) out << "  ";
                out << " ";
            }
            out << "| ";

            // The bytes, as characters
            for (MemAddr x = y; x < y + BYTES_PER_LINE; ++x)
            {
                char c = ' ';
                if (x >= addr && x < addr + size) {
                    c = buf[(size_t)(x - addr)];
                    c = (isprint(c) ? c : '.');
                }
                out << c;
            }
            out << endl;
        }
    }


}
