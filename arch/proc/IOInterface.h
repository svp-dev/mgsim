#ifndef IOINTERFACE_H
#define IOINTERFACE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

#include "IOResponseMultiplexer.h"
#include "IONotificationMultiplexer.h"
#include "IOBusInterface.h"
#include "IODirectCacheAccess.h"

class IOInterface : public Object, public Inspect::Interface<Inspect::Info>
{
public:
    class AsyncIOInterface : public MMIOComponent, public Inspect::Interface<Inspect::Info>
    {
    private:
        unsigned                m_devAddrBits;

        IOInterface&  GetInterface() const;
    public:
        AsyncIOInterface(const std::string& name, IOInterface& parent, Clock& clock, Config& config);

        size_t GetSize() const;

        Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
        Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

        void Cmd_Info(std::ostream& out, const std::vector<std::string>& args) const;
    };

    class PNCInterface : public MMIOComponent, public Inspect::Interface<Inspect::Info>
    {
    private:
        IOInterface&  GetInterface() const;

    public:
        PNCInterface(const std::string& name, IOInterface& parent, Clock& clock, Config& config);

        size_t GetSize() const;

        Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
        Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

        void Cmd_Info(std::ostream& out, const std::vector<std::string>& args) const;
    };
    

private:
    size_t                      m_numDevices;
    size_t                      m_numChannels;

    friend class AsyncIOInterface;
    AsyncIOInterface            m_async_io;

    friend class PNCInterface;
    PNCInterface                m_pnc;        

    IOResponseMultiplexer       m_rrmux;
    IONotificationMultiplexer   m_nmux;
    IOBusInterface              m_iobus_if;
    IODirectCacheAccess         m_dca;

    bool Read(IODeviceID dev, MemAddr address, MemSize size, const RegAddr& writeback);
    bool Write(IODeviceID dev, MemAddr address, const IOData& data);
    bool WaitForNotification(IOInterruptID dev, const RegAddr& writeback);
    Processor& GetProcessor() const;

public:
    IOInterface(const std::string& name, Processor& parent, Clock& clock, RegisterFile& rf, IIOBus& iobus, IODeviceID devid, Config& config);

    MMIOComponent& GetAsyncIOInterface() { return m_async_io; }
    MMIOComponent& GetPNCInterface() { return m_pnc; }

    IOResponseMultiplexer& GetReadResponseMultiplexer() { return m_rrmux; }
    IONotificationMultiplexer& GetNotificationMultiplexer() { return m_nmux; }
    IODirectCacheAccess& GetDirectCacheAccess() { return m_dca; }
    
    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& args) const;
};


#endif
