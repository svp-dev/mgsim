#include "DRISC.h"
#include <sim/config.h>

#include <sstream>
#include <iomanip>
#include <cstring>

using namespace std;

namespace Simulator
{
    DRISC::IOInterface::IOInterface(const string& name, DRISC& parent, Clock& clock, IMemory& memory, RegisterFile& rf, Allocator& alloc, IIOBus& iobus, IODeviceID devid, Config& config)
        : Object(name, parent, clock),
          m_numDevices(config.getValue<size_t>(*this, "NumDeviceSlots")),
          m_numChannels(config.getValue<size_t>(*this, "NumNotificationChannels")),
          m_async_io("aio",    *this, clock, config),
          m_pnc     ("pnc",    *this, clock, config),
          m_rrmux   ("rrmux",  *this, clock, rf, alloc, m_numDevices, config),
          m_nmux    ("nmux",   *this, clock, rf, alloc, m_numChannels, config),
          m_iobus_if("bus_if", *this, iobus.GetClock(), m_rrmux, m_nmux, m_dca, iobus, devid, config),
          m_dca     ("dca",    *this, clock, parent, memory, m_iobus_if, config)
    {
        if (m_numDevices == 0)
        {
            throw InvalidArgumentException(*this, "NumDeviceSlots not specified or zero");
        }
        if (m_numChannels == 0)
        {
            throw InvalidArgumentException(*this, "NumNotificationChannels not specified or zero");
        }
    }

    bool DRISC::IOInterface::Read(IODeviceID dev, MemAddr address, MemSize size, const RegAddr& writeback)
    {
        if (!m_rrmux.QueueWriteBackAddress(dev, writeback))
        {
            DeadlockWrite("Unable to queue the writeback address %s for I/O read to %u:%016llx (%u)",
                          writeback.str().c_str(), (unsigned)dev, (unsigned long long)address, (unsigned) size);
            return false;
        }

        IOBusInterface::IORequest req;
        req.device = dev;
        req.address = address;
        req.type = IOBusInterface::REQ_READ;
        req.data.size = size;

        if (!m_iobus_if.SendRequest(req))
        {
            DeadlockWrite("Unable to queue I/O read request to %u:%016llx (%u)",
                          (unsigned)dev, (unsigned long long)address, (unsigned) size);
            return false;
        }

        return true;
    }

    bool DRISC::IOInterface::Write(IODeviceID dev, MemAddr address, const IOData& data)
    {
        IOBusInterface::IORequest req;
        req.device = dev;
        req.address = address;
        req.type = IOBusInterface::REQ_WRITE;
        req.data = data;

        if (!m_iobus_if.SendRequest(req))
        {
            DeadlockWrite("Unable to queue I/O write request to %u:%016llx (%u)",
                          (unsigned)dev, (unsigned long long)address, (unsigned) data.size);
            return false;
        }

        return true;
    }

    bool DRISC::IOInterface::WaitForNotification(IONotificationChannelID dev, const RegAddr& writeback)
    {
        if (!m_nmux.SetWriteBackAddress(dev, writeback))
        {
            DeadlockWrite("Unable to set the writeback address %s for wait on channel %u",
                          writeback.str().c_str(), (unsigned)dev);
            return false;
        }
        return true;
    }

    bool DRISC::IOInterface::ConfigureNotificationChannel(IONotificationChannelID dev, Integer mode)
    {
        if (!m_nmux.ConfigureChannel(dev, mode))
        {
            DeadlockWrite("Unable to configure channel %u with mode %u",
                          (unsigned)dev, (unsigned)mode);
            return false;
        }
        return true;
    }

    void DRISC::IOInterface::Cmd_Info(ostream& out, const vector<string>& /*args*/) const
    {
        out << "This I/O interface is composed of the following components:" << endl
            << "- " << m_async_io.GetFQN() << endl
            << "- " << m_pnc.GetFQN() << endl
            << "- " << m_rrmux.GetFQN() << endl
            << "- " << m_nmux.GetFQN() << endl
            << "- " << m_dca.GetFQN() << endl
            << "Use 'info' on the individual components for more details." << endl;
    }

    DRISC::IOInterface::AsyncIOInterface::AsyncIOInterface(const string& name, DRISC::IOInterface& parent, Clock& clock, Config& config)
        : MMIOComponent(name, parent, clock),
          m_baseAddr(config.getValue<unsigned>(*this, "MMIO_BaseAddr")),
          m_devAddrBits(config.getValue<unsigned>(*this, "DeviceAddressBits"))
    {
        if (m_devAddrBits == 0)
        {
            throw InvalidArgumentException(*this, "DeviceAddressBits not set or zero");
        }
    }

    DRISC&
    DRISC::IOInterface::GetDRISC() const
    {
        return *static_cast<DRISC*>(GetParent());
    }

    DRISC::IOInterface&
    DRISC::IOInterface::AsyncIOInterface::GetInterface() const
    {
        return *static_cast<IOInterface*>(GetParent());
    }

    size_t DRISC::IOInterface::AsyncIOInterface::GetSize() const
    {
        return GetInterface().m_numDevices << m_devAddrBits;
    }

    MemAddr DRISC::IOInterface::AsyncIOInterface::GetDeviceBaseAddress(IODeviceID dev) const
    {
        assert(dev < GetInterface().m_numDevices);
        return m_baseAddr | (dev << m_devAddrBits);
    }

    Result DRISC::IOInterface::AsyncIOInterface::Read(MemAddr address, void* /*data*/, MemSize size, LFID fid, TID tid, const RegAddr& writeback)
    {
        IODeviceID dev = address >> m_devAddrBits;
        IOInterface& iface = GetInterface();

        if (dev > iface.m_numDevices)
        {
            throw exceptf<SimulationException>("Invalid I/O read to non-existent device %u by F%u/T%u", (unsigned)dev, (unsigned)fid, (unsigned)tid);
        }

        MemAddr devaddr = address & ((1ULL << m_devAddrBits) - 1);

        if (!GetInterface().Read(dev, devaddr, size, writeback))
        {
            return FAILED;
        }
        return DELAYED;
    }

    Result DRISC::IOInterface::AsyncIOInterface::Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid)
    {
        IODeviceID dev = address >> m_devAddrBits;
        if (dev > GetInterface().m_numDevices)
        {
            throw exceptf<SimulationException>("Invalid I/O read to non-existent device %u by F%u/T%u", (unsigned)dev, (unsigned)fid, (unsigned)tid);
        }

        MemAddr devaddr = address & ((1ULL << m_devAddrBits) - 1);

        IOData iodata;
        assert(size <= sizeof(iodata.data));
        memcpy(iodata.data, data, size);
        iodata.size = size;

        if (!GetInterface().Write(dev, devaddr, iodata))
        {
            return FAILED;
        }
        return SUCCESS;
    }

    void DRISC::IOInterface::AsyncIOInterface::Cmd_Info(ostream& out, const vector<string>& /*args*/) const
    {
        out <<
            "The asynchronous I/O interface accepts read and write commands from\n"
            "the processor and forwards them to the I/O bus. Each device is mapped\n"
            "to a fixed range in the memory address space.\n"
            "Start address    | End address      | Description\n"
            "-----------------+------------------+-------------------------\n"
            << hex << setfill('0');
        MemAddr size = 1ULL << m_devAddrBits;
        for (size_t i = 0; i < GetInterface().m_numDevices; ++i)
        {
            MemAddr begin = i << m_devAddrBits;
            out << setw(16) << begin
                << " | "
                << setw(16) << begin + size - 1
                << " | async. I/O range for device " << dec << i << hex
                << endl;
        }

    }

    DRISC::IOInterface::PNCInterface::PNCInterface(const string& name, DRISC::IOInterface& parent, Clock& clock, Config& config)
        : MMIOComponent(name, parent, clock),
          m_baseAddr(config.getValue<unsigned>(*this, "MMIO_BaseAddr"))
    {
    }

    DRISC::IOInterface&
    DRISC::IOInterface::PNCInterface::GetInterface() const
    {
        return *static_cast<IOInterface*>(GetParent());
    }

    size_t DRISC::IOInterface::PNCInterface::GetSize() const
    {
        return GetInterface().m_numChannels * sizeof(Integer);
    }

    Result DRISC::IOInterface::PNCInterface::Read(MemAddr address, void* /*data*/, MemSize size, LFID fid, TID tid, const RegAddr& writeback)
    {
        if (address % sizeof(Integer) != 0 || size != sizeof(Integer))
        {
            throw exceptf<SimulationException>("Invalid unaligned PNC read: %#016llx (%u)", (unsigned long long)address, (unsigned)size);
        }

        IONotificationChannelID which = address / sizeof(Integer);
        if (which > GetInterface().m_numChannels)
        {
            throw exceptf<SimulationException>("Invalid wait to non-existent notification/interrupt channel %u by F%u/T%u", (unsigned)which, (unsigned)fid, (unsigned)tid);
        }

        if (!GetInterface().WaitForNotification(which, writeback))
        {
            return FAILED;
        }
        return DELAYED;
    }


    void DRISC::IOInterface::PNCInterface::Cmd_Info(ostream& out, const vector<string>& /*args*/) const
    {
        out <<
            "The PNC interface accepts read commands from the processor and \n"
            "configures the I/O interface to wait for a notification or interrupt\n"
            "request coming from the I/O Bus. Each notification/interrupt channel\n"
            "is mapped to a fixed range in the memory address space.\n"
            "Address          | Description\n"
            "-----------------+----------------------\n"
            << hex << setfill('0');
        for (size_t i = 0; i < GetInterface().m_numChannels; ++i)
        {
            MemAddr begin = i * sizeof(Integer);
            out << setw(16) << begin
                << " | wait address for interrupt channel " << dec << i << hex
                << endl;
        }

    }

    Result DRISC::IOInterface::PNCInterface::Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid)
    {
        if (address % sizeof(Integer) != 0 || size != sizeof(Integer))
        {
            throw exceptf<SimulationException>("Invalid unaligned PNC read: %#016llx (%u)", (unsigned long long)address, (unsigned)size);
        }

        IONotificationChannelID which = address / sizeof(Integer);
        if (which > GetInterface().m_numChannels)
        {
            throw exceptf<SimulationException>("Invalid wait to non-existent notification/interrupt channel %u by F%u/T%u", (unsigned)which, (unsigned)fid, (unsigned)tid);
        }

        Integer value = UnserializeRegister(RT_INTEGER, data, size);

        if (!GetInterface().ConfigureNotificationChannel(which, value))
        {
            return FAILED;
        }
        return SUCCESS;
    }

    MemAddr DRISC::IOInterface::PNCInterface::GetDeviceBaseAddress(IODeviceID dev) const
    {
        assert(dev < GetInterface().m_numChannels);
        return m_baseAddr | (dev * sizeof(Integer));
    }

    void DRISC::IOInterface::Initialize(IODeviceID smcid)
    {
        // set up the core ASR to indicate the I/O parameters.
        // ASR_IO_PARAMS1 has 32 bits:
        // bits 0-7:   number of I/O devices mapped to the AIO
        // bits 8-15:  number of notification channels mapped to the PNC
        // bits 16-23: device ID of the SMC (enumeration) device on the I/O bus
        // bits 24-31: device ID of this core on the I/O bus
        // ASR_IO_PARAMS2 has 32 bits:
        // bits 0-7:   log2 of the AIO address space per device
        // bits 8-31:  (unused)
        assert(m_numDevices < 256);
        assert(m_numChannels < 256);
        assert(smcid < 256);
        IODeviceID devid = m_iobus_if.GetHostID();
        assert(devid < 256);
        Integer value =
            m_numDevices |
            m_numChannels << 8 |
            smcid << 16 |
            devid << 24;
        GetDRISC().WriteASR(ASR_IO_PARAMS1, value);
        value = m_async_io.GetDeviceAddressBits();
        GetDRISC().WriteASR(ASR_IO_PARAMS2, value);
        GetDRISC().WriteASR(ASR_AIO_BASE, m_async_io.GetDeviceBaseAddress(0));
        GetDRISC().WriteASR(ASR_PNC_BASE, m_pnc.GetDeviceBaseAddress(0));
    }
}
