#ifndef IOINTERFACE_H
#define IOINTERFACE_H

#include <sim/kernel.h>
#include <sim/inspect.h>
#include <sim/storage.h>
#include <arch/Memory.h>
#include "IOMatchUnit.h"
#include "IOResponseMultiplexer.h"
#include "IONotificationMultiplexer.h"
#include "IOBusInterface.h"
#include "IODirectCacheAccess.h"

namespace Simulator
{

class DRISC;

namespace drisc
{

class IOInterface : public Object, public Inspect::Interface<Inspect::Info>
{
public:
    class AsyncIOInterface : public drisc::MMIOComponent, public Inspect::Interface<Inspect::Info>
    {
    private:
        MemAddr                 m_baseAddr;
        unsigned                m_devAddrBits;

        IOInterface&  GetInterface() const;
        Object& GetDRISCParent() const { return *GetParent()->GetParent(); };
    public:
        AsyncIOInterface(const std::string& name, IOInterface& parent, Clock& clock, Config& config);

        size_t GetSize() const;

        Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
        Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

        void Cmd_Info(std::ostream& out, const std::vector<std::string>& args) const;

        // for boot sequence
        unsigned GetDeviceAddressBits() const { return m_devAddrBits; }
        MemAddr GetDeviceBaseAddress(IODeviceID dev) const;
    };

    class PNCInterface : public drisc::MMIOComponent, public Inspect::Interface<Inspect::Info>
    {
    private:
        MemAddr                 m_baseAddr;
        IOInterface&  GetInterface() const;
        Object& GetDRISCParent() const { return *GetParent()->GetParent(); };

    public:
        PNCInterface(const std::string& name, IOInterface& parent, Clock& clock, Config& config);

        size_t GetSize() const;

        Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
        Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

        void Cmd_Info(std::ostream& out, const std::vector<std::string>& args) const;

        // for boot sequence
        MemAddr GetDeviceBaseAddress(IODeviceID dev) const;
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
    bool WaitForNotification(IONotificationChannelID dev, const RegAddr& writeback);
    bool ConfigureNotificationChannel(IONotificationChannelID dev, Integer mode);
    Object& GetDRISCParent() const { return *GetParent(); };

public:
    IOInterface(const std::string& name, DRISC& parent, Clock& clock, IIOBus& iobus, IODeviceID devid, Config& config);
    void ConnectMemory(IMemory* memory);

    drisc::MMIOComponent& GetAsyncIOInterface() { return m_async_io; }
    drisc::MMIOComponent& GetPNCInterface() { return m_pnc; }

    IOResponseMultiplexer& GetReadResponseMultiplexer() { return m_rrmux; }
    IONotificationMultiplexer& GetNotificationMultiplexer() { return m_nmux; }
    IODirectCacheAccess& GetDirectCacheAccess() { return m_dca; }
    IOBusInterface&      GetIOBusInterface()    { return m_iobus_if; }

    MemAddr GetDeviceBaseAddress(IODeviceID dev) const { return m_async_io.GetDeviceBaseAddress(dev); }

    void Initialize();

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& args) const;
};

}
}

#endif
