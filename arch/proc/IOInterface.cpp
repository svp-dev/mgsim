#include "Processor.h"
#include "sim/config.h"
#include <sstream>

namespace Simulator
{
    Processor::IOInterface::IOInterface(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, IIOBus& iobus, IODeviceID devid, const Config& config)
        : Object(name, parent, clock),
          m_numDevices(config.getValue<size_t>("AsyncIONumDeviceSlots", 16)),
          m_async_io("aio",    *this, clock, m_numDevices, config),
          m_pic     ("pic",    *this, clock, m_numDevices, config),
          m_rrmux   ("rrmux",  *this, clock, rf, m_numDevices, config),
          m_intmux  ("intmux", *this, clock, rf, m_iobus_if, m_numDevices),
          m_iobus_if("bus_if", *this, clock, m_rrmux, m_intmux, iobus, devid, config)
    {
    }
    
    bool Processor::IOInterface::Read(IODeviceID dev, MemAddr address, MemSize size, const RegAddr& writeback)
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
        req.write = false;
        req.data.size = size;

        if (!m_iobus_if.SendRequest(req))
        {
            DeadlockWrite("Unable to queue I/O read request to %u:%016llx (%u)",
                          (unsigned)dev, (unsigned long long)address, (unsigned) size);
            return false;
        }

        return true;
    }

    bool Processor::IOInterface::Write(IODeviceID dev, MemAddr address, const IOData& data)
    {
        IOBusInterface::IORequest req;
        req.device = dev;
        req.address = address;
        req.write = true;
        req.data = data;

        if (!m_iobus_if.SendRequest(req))
        {
            DeadlockWrite("Unable to queue I/O write request to %u:%016llx (%u)",
                          (unsigned)dev, (unsigned long long)address, (unsigned) data.size);
            return false;
        }

        return true;
    }

    bool Processor::IOInterface::WaitForNotification(IODeviceID dev, const RegAddr& writeback)
    {
        if (!m_intmux.SetWriteBackAddress(dev, writeback))
        {
            DeadlockWrite("Unable to set the writeback address %s for wait on device %u",
                          writeback.str().c_str(), (unsigned)dev);
            return false;
        }
        return true;
    }

    Processor::IOInterface::AsyncIOInterface::AsyncIOInterface(const std::string& name, Processor::IOInterface& parent, Clock& clock, size_t numDevices, const Config& config)
        : MMIOComponent(name, parent, clock),
          m_devAddrBits(config.getValue<unsigned>("AsyncIODeviceAddressBits", 24)),
          m_numDeviceSlots(numDevices)
    {
        
    }

    Processor::IOInterface&
    Processor::IOInterface::AsyncIOInterface::GetInterface()
    {
        return *static_cast<IOInterface*>(GetParent());
    }

    size_t Processor::IOInterface::AsyncIOInterface::GetSize() const
    {
        return m_numDeviceSlots << m_devAddrBits;
    }

    Result Processor::IOInterface::AsyncIOInterface::Read(MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback)
    {
        IODeviceID dev = address >> m_devAddrBits;
        if (dev > m_numDeviceSlots)
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

    Result Processor::IOInterface::AsyncIOInterface::Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid)
    {
        IODeviceID dev = address >> m_devAddrBits;
        if (dev > m_numDeviceSlots)
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

    Processor::IOInterface::PICInterface::PICInterface(const std::string& name, Processor::IOInterface& parent, Clock& clock, size_t numDevices, const Config& config)
        : MMIOComponent(name, parent, clock),
          m_numDeviceSlots(numDevices)
    {
    }

    Processor::IOInterface&
    Processor::IOInterface::PICInterface::GetInterface()
    {
        return *static_cast<IOInterface*>(GetParent());
    }

    size_t Processor::IOInterface::PICInterface::GetSize() const
    {
        return m_numDeviceSlots * sizeof(Integer);
    }

    Result Processor::IOInterface::PICInterface::Read(MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback)
    {
        IODeviceID dev = address / sizeof(Integer);
        if (dev > m_numDeviceSlots)
        {
            throw exceptf<SimulationException>("Invalid wait to non-existent device %u by F%u/T%u", (unsigned)dev, (unsigned)fid, (unsigned)tid);
        }
        
        if (!GetInterface().WaitForNotification(dev, writeback))
        {
            return FAILED;
        }
        return DELAYED;
    }

    Result Processor::IOInterface::PICInterface::Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid)
    { 
        return FAILED;
    }
}
