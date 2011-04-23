#ifndef IOINTERFACE_H
#define IOINTERFACE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

#include "IOResponseMultiplexer.h"
#include "IOInterruptMultiplexer.h"
#include "IOBusInterface.h"

class IOInterface : public Object
{
private:
    size_t                      m_numDevices;

    class AsyncIOInterface : public MMIOComponent
    {
    private:
        unsigned                m_devAddrBits;
        size_t                  m_numDeviceSlots;

        IOInterface&  GetInterface();
    public:
        AsyncIOInterface(const std::string& name, IOInterface& parent, Clock& clock, size_t numDevices, const Config& config);

        size_t GetSize() const;

        Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
        Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);
    };

    friend class AsyncIOInterface;
    AsyncIOInterface m_async_io;

    class PICInterface : public MMIOComponent
    {
    private:
        size_t                  m_numDeviceSlots;

        IOInterface&  GetInterface();
    public:
        PICInterface(const std::string& name, IOInterface& parent, Clock& clock, size_t numDevices, const Config& config);

        size_t GetSize() const;

        Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
        Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);
    };
    
    friend class PICInterface;
    PICInterface m_pic;        

    IOResponseMultiplexer   m_rrmux;
    IOInterruptMultiplexer  m_intmux;
    IOBusInterface          m_iobus_if;

    bool Read(IODeviceID dev, MemAddr address, MemSize size, const RegAddr& writeback);
    bool Write(IODeviceID dev, MemAddr address, const IOData& data);
    bool WaitForNotification(IODeviceID dev, const RegAddr& writeback);

public:
    IOInterface(const std::string& name, Object& parent, Clock& clock, RegisterFile& rf, IIOBus& iobus, const Config& config);

    MMIOComponent& GetAsyncIOInterface() { return m_async_io; }
    MMIOComponent& GetPICInterface() { return m_pic; }

};


#endif
