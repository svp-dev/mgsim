#ifndef THREADTABLE_H
#define THREADTABLE_H

#include "ports.h"

class Config;

namespace Simulator
{

class Processor;

struct Thread
{
    struct RegInfo
    {
        RegIndex base;
        RegIndex producer;
    };
    
    struct Dependencies
    {
        bool         killed;
        bool         nextKilled;
        bool         prevCleanedUp;
        unsigned int numPendingWrites;
    };

    MemAddr      pc;
    RegInfo      regs[NUM_REG_TYPES];
    Dependencies dependencies;
    bool         isLastThreadInBlock;
    bool         isFirstThreadInFamily;
    bool         isLastThreadInFamily;
    bool         waitingForWrites;
    TID          prevInBlock;
    TID          nextInBlock;
    CID          cid;
    LFID         family;
    TID          nextState;
    TID          nextMember;

    // Architecture specific per-thread stuff
#if TARGET_ARCH == ARCH_ALPHA
	FPCR         fpcr;
#elif TARGET_ARCH == ARCH_SPARC
    PSR          psr;
    FSR          fsr;
    uint32_t     Y;
#endif    

    // Admin
    uint64_t    index;
    ThreadState state;
};

class ThreadTable : public Object
{
public:
    ThreadTable(const std::string& name, Processor& parent, const Config& config);

    TSize GetNumThreads() const { return m_threads.size(); }

    typedef Thread value_type;
          Thread& operator[](TID index)       { return m_threads[index]; }
    const Thread& operator[](TID index) const { return m_threads[index]; }

    TID   PopEmpty(ContextType type);
    void  PushEmpty(const ThreadQueue& queue, ContextType context);
    void  ReserveThread();
    void  UnreserveThread();
    TSize GetNumFreeThreads() const;
    
    bool IsEmpty() const;
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    ThreadQueue         m_empty;
    std::vector<Thread> m_threads;
    TSize               m_free[NUM_CONTEXT_TYPES];
};

}
#endif

