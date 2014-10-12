// -*- c++ -*-
#ifndef ICACHE_H
#define ICACHE_H

#include "sim/kernel.h"
#include "sim/inspect.h"
#include "sim/buffer.h"
#include "arch/Memory.h"
#include "forward.h"

namespace Simulator
{
namespace drisc
{

class ICache : public Object, public IMemoryCallback, public Inspect::Interface<Inspect::Read>
{
    friend class Simulator::DRISC;

    enum LineState
    {
        LINE_EMPTY,      ///< Line is not in use
        LINE_LOADING,    ///< Line is being loaded
        LINE_INVALID,    ///< Line has been invalidated but still has a pending load
        LINE_FULL,       ///< Line has data and can be reused
    };

    /// A Cache-line
    struct Line
    {
        MemAddr       tag;          ///< Address tag
        char*         data;         ///< The line data
        CycleNo       access;       ///< Last access time (for LRU replacement)
        ThreadQueue   waiting;      ///< Threads waiting on this line
        unsigned long references;   ///< Number of references to this line
        LineState     state;        ///< The state of the line
        bool          creation;             ///< Is the family creation process waiting on this line?

        template<typename A>
        void serialize(A& arch) { arch & tag & access & waiting & references & state & creation; }
    };

    Result Fetch(MemAddr address, MemSize size, TID* tid, CID* cid);
    Result FindLine(MemAddr address, Line* &line, bool check_only = false);

    // Processes
    Result DoOutgoing();
    Result DoIncoming();

    IMemory*          m_memory;
    IBankSelector*    m_selector;
    MCID              m_mcid;
    std::vector<Line> m_lines;
    std::vector<char> m_data;
    Buffer<MemAddr>   m_outgoing;
    Buffer<CID>       m_incoming;

    size_t            m_lineSize;
    size_t            m_assoc;

    // Statistics:
    DefineSampleVariable(uint64_t, numHits);
    DefineSampleVariable(uint64_t, numDelayedReads);
    DefineSampleVariable(uint64_t, numEmptyMisses);
    DefineSampleVariable(uint64_t, numLoadingMisses);
    DefineSampleVariable(uint64_t, numInvalidMisses);
    DefineSampleVariable(uint64_t, numHardConflicts);
    DefineSampleVariable(uint64_t, numResolvedConflicts);
    DefineSampleVariable(uint64_t, numStallingMisses);

    Object& GetDRISCParent() const { return *GetParent(); }

public:
    ICache(const std::string& name, DRISC& parent, Clock& clock);
    ICache(const ICache&) = delete;
    ICache& operator=(const ICache&) = delete;
    ~ICache();
    void ConnectMemory(IMemory* memory);

    // Processes
    Process p_Outgoing;
    Process p_Incoming;

    ArbitratedService<> p_service;

    Result Fetch(MemAddr address, MemSize size, CID& cid); // Initial family line fetch
    Result Fetch(MemAddr address, MemSize size, TID& tid, CID& cid);  // Thread code fetch
    bool   Read(CID cid, MemAddr address, void* data, MemSize size) const;
    bool   ReleaseCacheLine(CID bid);
    bool   IsEmpty() const;

    // IMemoryCallback
    bool   OnMemoryReadCompleted(MemAddr addr, const char* data) override;
    bool   OnMemoryWriteCompleted(TID tid) override;
    bool   OnMemorySnooped(MemAddr addr, const char* data, const bool* mask) override;
    bool   OnMemoryInvalidated(MemAddr addr) override ;
    Object& GetMemoryPeer() override;

    // Admin
    size_t GetLineSize() const { return m_lineSize; }
    size_t GetAssociativity() const { return m_assoc; }
    size_t GetNumLines() const { return m_lines.size(); }
    size_t GetNumSets() const { return GetNumLines() / m_assoc; }

    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;
};

}
}

#endif
