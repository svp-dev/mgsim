#ifndef THREADTABLE_H
#define THREADTABLE_H

#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

struct Thread
{
    // Register information
    struct RegInfo
    {
        // The base to this thread's locals, dependents and shareds
        RegIndex locals;
        RegIndex dependents;
        RegIndex shareds;
    };
    
    // The dependencies that must be resolved before a thread can be cleaned up
    struct Dependencies
    {
        /*
         Obviously the thread must have terminated before it can be cleaned up.
        */
        bool killed;
        
        /*
         Threads can terminate out of index order. This leads to problems
         when figuring out which thread is the logical predecessor of the
         next created thread.
         For local creates, the family's lastAllocated field can be used
         to find the last allocated thread, which is always the predecessor
         of the next created thread. However, in group creates, threads must
         be linked up across cores. The family's firstInBlock field is
         used for this. However, if threads complete out of order,
         thise field can be overwritten with the next block's first
         thread, causing the border thread on the previous core to link
         up with the wrong thread.
         To counter this, sequential thread cleanup is enforced by introducing
         a dependency on the previous thread's cleanup event.
        */
        bool prevCleanedUp;
        
        /*
         All writes made by a thread are tagged with the thread's TID. Thus,
         the thread cannot be cleaned up until those writes have been
         confirmed by the memory system.
        */
        unsigned int numPendingWrites;
    };
    
    MemAddr      pc;
    RegInfo      regs[NUM_REG_TYPES];
    Dependencies dependencies;
    bool         waitingForWrites;
    TID          nextInBlock;
    CID          cid;
    LFID         family;
    TID          next;

    // Architecture specific per-thread stuff
#if defined(TARGET_MTALPHA)
	FPCR         fpcr;
#elif defined(TARGET_MTSPARC)
    PSR          psr;
    FSR          fsr;
    uint32_t     Y;
#elif defined(TARGET_MIPS32) || defined(TARGET_MIPS32EL)
    // FIXME: FILL IN THREAD-SPECIFIC DATA FIELDS HERE
#endif    

    // Admin
    uint64_t    index;
    ThreadState state;
};

class ThreadTable : public Object, public Inspect::Interface<Inspect::Read>
{
public:
    ThreadTable(const std::string& name, Processor& parent, Clock& clock, Config& config);

    TSize GetNumThreads() const { return m_threads.size(); }

    typedef Thread value_type;
          Thread& operator[](TID index)       { return m_threads[index]; }
    const Thread& operator[](TID index) const { return m_threads[index]; }

    TID   PopEmpty(ContextType type);
    void  PushEmpty(TID tid, ContextType context);
    void  ReserveThread();
    void  UnreserveThread();
    TSize GetNumFreeThreads(ContextType type) const;
    
    bool IsEmpty() const;
    
    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

    // Statistics
    TSize GetTotalAllocated() { UpdateStats(); return m_totalalloc; }
    TSize GetMaxAllocated() const { return m_maxalloc; }

private:
    TID                 m_empty;
    std::vector<Thread> m_threads;
    TSize               m_free[NUM_CONTEXT_TYPES];

    // Admin
    TSize               m_totalalloc;
    TSize               m_maxalloc;
    CycleNo             m_lastcycle;
    TSize               m_curalloc;
    Processor&          m_parent;

    void UpdateStats();
    void CheckStateSanity() const;
};

#endif

