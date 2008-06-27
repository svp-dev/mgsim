#ifndef THREADTABLE_H
#define THREADTABLE_H

#include "ports.h"

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
	FPCR         fpcr;
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

    // Admin
    uint64_t    index;
    ThreadState state;
};

class ThreadTable : public Structure<TID>
{
public:
	struct Config
	{
		TSize numThreads;
	};

    ThreadTable(Processor& parent, const Config& config);

    TSize getNumThreads() const { return m_threads.size(); }

          Thread& operator[](TID index)       { return m_threads[index]; }
    const Thread& operator[](TID index) const { return m_threads[index]; }

    bool empty() const { return m_numUsed == 0; }

    TID  popEmpty();
    bool pushEmpty(const ThreadQueue& queue);

    //
    // Ports
    //
    DedicatedReadPort       p_fetch;
    DedicatedWritePort<TID> p_execute;

private:
    Processor&          m_parent;
    ThreadQueue         m_empty;
    std::vector<Thread> m_threads;
    unsigned long       m_numUsed;
};

}
#endif

