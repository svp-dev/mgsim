#ifndef DCACHE_H
#define DCACHE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class DCache : public Object, public IMemoryCallback, public Inspect::Interface<Inspect::Read>
{
    friend class Processor;

public:
    /// The state of a cache-line
    enum LineState
    {
        LINE_EMPTY,      ///< Line is empty.
        LINE_LOADING,    ///< Line is being loaded.
        LINE_INVALID,    ///< Line is invalid.
        LINE_FULL        ///< Line is full.
    };

    struct Line
    {
        MemAddr     tag;        ///< The address tag.
        char*       data;       ///< The data in this line.
        bool*       valid;      ///< A bitmap of valid bytes in this line.
        CycleNo     access;     ///< Last access time of this line (for LRU).
        RegAddr     waiting;    ///< First register waiting on this line.
        LineState   state;      ///< The line state.
        bool        processing; ///< Has the line been added to m_returned yet?
        bool        create;     ///< Is the line expected by the create process (bundle)?
    };

private:
    struct Request
    {
        MemData   data;
        MemAddr   address;
        WClientID wid;
        bool      write;
    };

    struct Response
    {
        bool write;
        union {
            WClientID wid;
            CID       cid;
        };
    };

    // Information for multi-register writes
    struct WritebackState
    {
        uint64_t     value;  ///< Value to write
        RegAddr      addr;   ///< Address of the next register to write
        RegAddr      next;   ///< Next register after this one
        unsigned int size;   ///< Number of registers remaining to write
        unsigned int offset; ///< Current offset in the multi-register operand
        LFID         fid;    ///< FID of the thread's that's waiting on the register
    };

    Result FindLine(MemAddr address, Line* &line, bool check_only);

    Processor&           m_parent;          ///< Parent processor.
    Allocator&           m_allocator;       ///< Allocator component.
    FamilyTable&         m_familyTable;     ///< Family table .
    RegisterFile&        m_regFile;         ///< Register File.
    IMemory&             m_memory;          ///< Memory
    MCID                 m_mcid;            ///< Memory Client ID
    std::vector<Line>    m_lines;           ///< The cache-lines.
    size_t               m_assoc;           ///< Config: Cache associativity.
    size_t               m_sets;            ///< Config: Number of sets in the cace.
    size_t               m_lineSize;        ///< Config: Size of a cache line, in bytes.
    IBankSelector*       m_selector;        ///< Mapping of cache line addresses to tags and set indices.
    Buffer<CID>          m_completed;       ///< Completed cache-line reads waiting to be processed.
    Buffer<Response>     m_incoming;        ///< Incoming buffer from memory bus.
    Buffer<Request>      m_outgoing;        ///< Outgoing buffer to memory bus.
    WritebackState       m_wbstate;         ///< Writeback state


    // Statistics

    uint64_t             m_numRHits;
    uint64_t             m_numDelayedReads;
    uint64_t             m_numEmptyRMisses;
    uint64_t             m_numInvalidRMisses;
    uint64_t             m_numLoadingRMisses;
    uint64_t             m_numHardConflicts;
    uint64_t             m_numResolvedConflicts;

    uint64_t             m_numWAccesses;
    uint64_t             m_numWHits;
    uint64_t             m_numPassThroughWMisses;
    uint64_t             m_numLoadingWMisses;

    uint64_t             m_numStallingRMisses;
    uint64_t             m_numStallingWMisses;

    uint64_t             m_numSnoops;


    Result DoCompletedReads();
    Result DoIncomingResponses();
    Result DoOutgoingRequests();

public:
    DCache(const std::string& name, Processor& parent, Clock& clock, Allocator& allocator, FamilyTable& familyTable, RegisterFile& regFile, IMemory& memory, Config& config);
    DCache(const DCache&) = delete;
    DCache& operator=(const DCache&) = delete;
    ~DCache();

    // Processes
    Process p_CompletedReads;
    Process p_Incoming;
    Process p_Outgoing;

    ArbitratedService<> p_service;

    // Public interface
    Result Read (MemAddr address, void* data, MemSize size, RegAddr* reg);
    Result Write(MemAddr address, void* data, MemSize size, LFID fid, TID tid);

    size_t GetLineSize() const { return m_lineSize; }

    // Memory callbacks
    bool OnMemoryReadCompleted(MemAddr addr, const char* data) override;
    bool OnMemoryWriteCompleted(TID tid) override;
    bool OnMemorySnooped(MemAddr addr, const char* data, const bool* mask) override;
    bool OnMemoryInvalidated(MemAddr addr) override;

    Object& GetMemoryPeer() override;


    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const override;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const override;

    size_t GetAssociativity() const { return m_assoc; }
    size_t GetNumLines()      const { return m_lines.size(); }
    size_t GetNumSets()       const { return m_sets; }

    const Line& GetLine(size_t i) const { return m_lines[i];  }
};

#endif
