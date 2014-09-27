// -*- c++ -*-
#ifndef ACTIVE_ROM_H
#define ACTIVE_ROM_H

#include "arch/IOBus.h"
#include "arch/Memory.h"
#include "sim/kernel.h"
#include "sim/config.h"
#include "sim/flag.h"
#include "sim/inspect.h"
#include <map>

namespace Simulator
{
    class ActiveROM : public IIOBusClient, public Object, public Inspect::Interface<Inspect::Info|Inspect::Read>
    {
    public:
        struct LoadableRange
        {
            size_t               rom_offset;
            MemSize              rom_size;
            MemAddr              vaddr;
            MemSize              vsize;
            IMemory::Permissions perm;
        };
    private:
        IMemoryAdmin&      m_memory;

        char              *m_data;
        size_t             m_lineSize;
        size_t             m_numLines;

        std::vector<LoadableRange> m_loadable;

        std::string        m_filename;
        const bool         m_verboseload;
        bool               m_bootable;
        MemAddr            m_start_address;
        bool               m_legacy;
        bool               m_booting;
        const bool         m_preloaded_at_boot;

        // DCA parameters

        IODeviceID         m_devid;
        IIOBus&            m_iobus;

        IODeviceID         m_client;
        IONotificationChannelID      m_completionTarget;

        Flag               m_loading;
        Flag               m_flushing;
        Flag               m_notifying;

        size_t             m_currentRange;
        size_t             m_currentOffset;

        void LoadConfig();
        void LoadArgumentVector();
        void LoadFile(const std::string& filename);
        void PrepareRanges();

    public:
        ActiveROM(const std::string& name, Object& parent, IMemoryAdmin& mem, IIOBus& iobus, IODeviceID devid, bool quiet = false);
        ActiveROM(const ActiveROM&) = delete;
        ActiveROM& operator=(const ActiveROM&) = delete;
        ~ActiveROM();

        void Initialize();

        Process p_Load;
        Process p_Flush;
        Process p_Notify;

        Result DoLoad();
        Result DoFlush();
        Result DoNotify();

        bool IsBootable() const { return m_bootable; }
        bool IsPreloaded() const { return m_preloaded_at_boot; }
        const std::string& GetProgramName() const { return m_filename; }
        IODeviceID GetDeviceID() const { return m_devid; }

        bool OnReadRequestReceived(IODeviceID from, MemAddr address, MemSize size);
        bool OnWriteRequestReceived(IODeviceID from, MemAddr address, const IOData& data);
        bool OnReadResponseReceived(IODeviceID from, MemAddr address, const IOData& data);

        StorageTraceSet GetWriteRequestTraces() const;
        StorageTraceSet GetReadResponseTraces() const;

        void GetDeviceIdentity(IODeviceIdentification& id) const;
        const std::string& GetIODeviceName() const;

        /* debug */
        void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
        void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    };


}


#endif
