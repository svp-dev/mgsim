#ifndef FAMILYTABLE_H
#define FAMILYTABLE_H

#include <queue>
#include "ports.h"

namespace Simulator
{

class Processor;

struct Family
{
    struct RegInfo
    {   
        RegIndex globals;  // Base address of the globals
        RegIndex shareds;  // Address of the shareds in the parent thread
        RegsNo   count;    // Number of globals, locals and shareds

        RegIndex base;     // Base address of this family's register block
        RegSize  size;     // Purely for simplicity, this could be calculated from other values
        RegIndex latest;   // Address of the last allocated thread
    };

    MemAddr      pc;             // Initial PC for newly created threads

	bool         created;	     // Has the family entry been used in a create yet?
    TSize        virtBlockSize;  // Virtual block size, calculated from start, end and step
    TSize        physBlockSize;  // Physical block size, <= Virtual block size, depending on the amount of free registers
    TSize        allocated;      // How many threads are currently allocated (0 <= allocated <= physBlockSize)
    int64_t      start;          // Start index of the family
    int64_t      step;           // Step size of the family
	Place        place;			 // Place where the family is to be created

	union {
		uint64_t nThreads;       // Number of threads we need to allocate
		int64_t  end;			 // Limit of the family
	};
    uint64_t     index;          // Index of the next to be allocated thread (0, 1, 2... lastThread)
    bool         allocationDone; // Are we done allocating?

    RemoteTID    parent;         // Parent thread
	GFID         gfid;			 // Corresponding global LFID
    
    bool         killed;
    bool         hasDependency;
    bool         prevTerminated;
	bool         createCompleted;
    unsigned int numPendingReads;
    unsigned int numPendingWrites;
    unsigned int numPendingShareds;
    unsigned int numThreadsRunning;
	unsigned int numThreadsQueued;

    RegAddr      exitCodeReg;
    RegAddr      exitValueReg;
    TID          lastAllocated;
    TID          lastThreadInBlock;
    TID          firstThreadInBlock;

    RegInfo      regs[NUM_REG_TYPES];    // Register information

    ThreadQueue  members;        // Queue of all threads in this family
    LFID         next;           // Next family in the empty or active family queue

    // Admin
    TSize       nRunning;       // Number of unkilled allocated threads
    FamilyState state;          // Family state
};

class FamilyTable : public Structure<LFID>
{
public:
	struct GlobalFamily
	{
		LFID fid;
		bool used;
	};

	struct Config
	{
		FSize numGlobals;
		FSize numFamilies;
	};

    FamilyTable(Processor& parent, const Config& config);
    
          Family& operator[](LFID fid)       { return m_families[fid]; }
	const Family& operator[](LFID fid) const { return m_families[fid]; }

	LFID AllocateFamily(GFID gfid = INVALID_GFID);
	LFID TranslateFamily(GFID gfid) const;
	GFID AllocateGlobal(LFID lfid);
    bool ReserveGlobal(GFID fid);
    bool UnreserveGlobal(GFID fid);
    bool FreeFamily(LFID fid);

    // Admin functions
    bool  empty() const { return m_numFamilies == 0; }
    const std::vector<GlobalFamily>& GetGlobals()  const { return m_globals; }
    const std::vector<Family>&       GetFamilies() const { return m_families; }

private:
    Processor&                m_parent;
    std::vector<GlobalFamily> m_globals;
    std::vector<Family>       m_families;
	FSize                     m_numFamilies;
    FamilyQueue               m_empty;
};

}
#endif

