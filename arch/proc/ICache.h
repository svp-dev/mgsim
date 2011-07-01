#ifndef ICACHE_H
#define ICACHE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class ICache : public Object, public IMemoryCallback, public Inspect::Interface<Inspect::Read>
{
    friend class Processor;
    
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
        LineState     state;        ///< The state of the line
        MemAddr       tag;			///< Address tag
        char*         data;			///< The line data
        CycleNo       access;		///< Last access time (for LRU replacement)
		bool          creation;		///< Is the family creation process waiting on this line?
        ThreadQueue	  waiting;		///< Threads waiting on this line
		unsigned long references;	///< Number of references to this line
	};
	
    Result Fetch(MemAddr address, MemSize size, TID* tid, CID* cid);
    Result FindLine(MemAddr address, Line* &line, bool check_only = false);
    
    // Processes
    Result DoOutgoing();
    Result DoIncoming();

    Processor&        m_parent;
	Allocator&        m_allocator;
	IMemory&          m_memory;
    IBankSelector*    m_selector;
    MCID              m_mcid;
    std::vector<Line> m_lines;
	std::vector<char> m_data;
	Buffer<MemAddr>   m_outgoing;
	Buffer<CID>       m_incoming;

    uint64_t             m_numHits;         ///< Number of hits so far.
    uint64_t             m_numEmptyMisses;  ///< Number of misses so far (miss to an empty cache line).
    uint64_t             m_numLoadingMisses;///< Number of misses so far (miss to a loading cache line with same tag).
    uint64_t             m_numInvalidMisses;///< Number of misses so far (miss to an invalid cache line with same tag).
    uint64_t             m_numHardConflicts;///< Number of conflicts so far (miss to a non-empty, non-reusable cache line with different tag).
    uint64_t             m_numResolvedConflicts;///< Number of resolved conflicts so far (miss to a non-empty, substitutable line with different tag).
    uint64_t             m_numStallingMisses;///< Number of miss stalls caused by busy memory

    size_t            m_lineSize;
    size_t            m_assoc;
    
public:
    ICache(const std::string& name, Processor& parent, Clock& clock, Allocator& allocator, IMemory& memory, Config& config);
    ~ICache();
    
    // Processes
    Process p_Outgoing;
    Process p_Incoming;

    ArbitratedService<> p_service;
    
    Result Fetch(MemAddr address, MemSize size, CID& cid);				// Initial family line fetch
    Result Fetch(MemAddr address, MemSize size, TID& tid, CID& cid);	// Thread code fetch
    bool   Read(CID cid, MemAddr address, void* data, MemSize size) const;
    bool   ReleaseCacheLine(CID bid);
    bool   IsEmpty() const;
    bool   OnMemoryReadCompleted(MemAddr addr, const MemData& data);
    bool   OnMemoryWriteCompleted(TID tid);
    bool   OnMemorySnooped(MemAddr addr, const MemData& data);
    bool   OnMemoryInvalidated(MemAddr addr);
    Object& GetMemoryPeer() { return m_parent; }
    size_t GetLineSize() const { return m_lineSize; }
    size_t GetAssociativity() const { return m_assoc; }
    
    // Debugging
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

#endif
