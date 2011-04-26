#include "Processor.h"
#include "sim/config.h"
#include <sstream>
#include <iomanip>

using namespace std;

namespace Simulator
{
    Processor::IOInterface::IOInterface(const string& name, Object& parent, Clock& clock, RegisterFile& rf, IIOBus& iobus, IODeviceID devid, const Config& config)
        : Object(name, parent, clock),
          m_numDevices(config.getValue<size_t>("AsyncIONumDeviceSlots", 8)),
          m_numInterrupts(config.getValue<size_t>("AsyncIONumInterruptChannels", 8)),
          m_async_io("aio",    *this, clock, m_numDevices, config),
          m_pic     ("pic",    *this, clock, m_numInterrupts, config),
          m_rrmux   ("rrmux",  *this, clock, rf, m_numDevices, config),
          m_intmux  ("intmux", *this, clock, rf, m_numInterrupts),
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

    Processor::IOInterface::AsyncIOInterface::AsyncIOInterface(const string& name, Processor::IOInterface& parent, Clock& clock, size_t numDevices, const Config& config)
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


    void Processor::IOInterface::AsyncIOInterface::Cmd_Info(ostream& out, const vector<string>& args) const
    {
        out << "Start address    | End address      | Description" << std::endl
            << "-----------------+------------------+-------------------------" << std::endl
            << hex << setfill('0');
        MemAddr size = 1ULL << m_devAddrBits;
        for (size_t i = 0; i < m_numDeviceSlots; ++i)
        {
            MemAddr begin = i << m_devAddrBits;
            MemAddr end = begin + size - 1;
            out << setw(16) << begin
                << " | "
                << setw(16) << begin + size - 1
                << " | async. I/O range for device " << dec << i << hex
                << endl;
        }
        
    }

    Processor::IOInterface::PICInterface::PICInterface(const string& name, Processor::IOInterface& parent, Clock& clock, size_t numInterrupts, const Config& config)
        : MMIOComponent(name, parent, clock),
          m_numInterrupts(numInterrupts)
    {
    }

    Processor::IOInterface&
    Processor::IOInterface::PICInterface::GetInterface()
    {
        return *static_cast<IOInterface*>(GetParent());
    }

    size_t Processor::IOInterface::PICInterface::GetSize() const
    {
        return m_numInterrupts * sizeof(Integer);
    }

    Result Processor::IOInterface::PICInterface::Read(MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback)
    {
        IOInterruptID which = address / sizeof(Integer);
        if (which > m_numInterrupts)
        {
            throw exceptf<SimulationException>("Invalid wait to non-existent interrupt channel %u by F%u/T%u", (unsigned)which, (unsigned)fid, (unsigned)tid);
        }
        
        if (!GetInterface().WaitForNotification(which, writeback))
        {
            return FAILED;
        }
        return DELAYED;
    }

    void Processor::IOInterface::PICInterface::Cmd_Info(ostream& out, const vector<string>& args) const
    {
        out << "Address          | Description" << std::endl
            << "-----------------+----------------------" << std::endl
            << hex << setfill('0');
        for (size_t i = 0; i < m_numInterrupts; ++i)
        {
            MemAddr begin = i * sizeof(Integer);
            out << setw(16) << begin
                << " | wait address for interrupt channel " << dec << i << hex
                << endl;
        }
        
    }

    Result Processor::IOInterface::PICInterface::Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid)
    { 
        return FAILED;
    }
}
