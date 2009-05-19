#ifndef FAMILYTABLE_H
#define FAMILYTABLE_H

#include "ports.h"
#include <queue>

namespace Simulator
{

class Processor;

struct Family
{
    enum Type
    {
        LOCAL,
        GROUP,
        DELEGATED,
    };

    struct RegInfo
    {   
        RegIndex globals;  // Base address of the globals
        RegIndex shareds;  // Address of the shareds in the parent thread
        RegsNo   count;    // Number of globals, locals and shareds

        RegIndex base;     // Base address of this family's register block
        RegSize  size;     // Purely for simplicity, this could be calculated from other values
        RegIndex latest;   // Address of the last allocated thread
    };
    
    // Groups all dependencies that need to be resolved before termination and cleanup
    struct Dependencies
    {
        bool         allocationDone;      // We are done allocating threads
        bool         prevSynchronized;    // Family has synchronized on the previous processor
        bool         nextTerminated;      // Family has terminated on the next processor
        TSize        numThreadsAllocated; // Number of threads currently allocated (0 <= allocated <= physBlockSize)
        unsigned int numPendingReads;     // Number of outstanding memory reads
        unsigned int numPendingShareds;   // Number of parent shareds still needing to be transmitted
	};

    Type         type;           // The type of the family
    MemAddr      pc;             // Initial PC for newly created threads
	bool         legacy;		 // Consists of a single thread of legacy code?
	bool         created;	     // Has the family entry been used in a create yet?
    uint64_t     virtBlockSize;  // Virtual block size
    TSize        physBlockSize;  // Physical block size, <= Virtual block size, depending on the amount of free registers
    int64_t      start;          // Start index of the family
    int64_t      step;           // Step size of the family
	PlaceID      place;		     // Place where the family is to be created
	bool         infinite;       // Is this an infinite family?
	union {
		uint64_t nThreads;       // Number of threads we need to allocate
		int64_t  limit;			 // Limit of the family
	};
    uint64_t     index;          // Index of the next to be allocated thread (0, 1, 2... nThreads-1)
    struct
    {
        LPID lpid;               // Parent for group creates (creating CPU for delegated)
        GPID gpid;               // Parent for delegated creates
        union
        {
            TID  tid;            // Parent thread for group creates
            LFID fid;            // Parent family for delegated creates
        };
    }            parent;         // Parent thread/family
    bool         hasDependency;  // Does this family use shareds?
    bool         killed;         // Has this family been killed?
    Dependencies dependencies;   // The dependencies for termination and cleanup
    ThreadQueue  members;        // Queue of all threads in this family
    LFID         next;           // Next family in the empty or active family queue
    LFID         link_next;      // The LFID of the matching family on the next CPU
    LFID         link_prev;      // The LFID of the matching family on the previous CPU
    
    RegIndex     exitCodeReg;
    TID          lastAllocated;
    TID          lastThreadInBlock;
    TID          firstThreadInBlock;

    RegInfo      regs[NUM_REG_TYPES];    // Register information

    // Admin
    FamilyState  state;          // Family state
};

class FamilyTable : public Structure<LFID>
{
public:
	struct Config
	{
		FSize numFamilies;
	};

    FamilyTable(Processor& parent, const Config& config);
    
          Family& operator[](LFID fid)       { return m_families[fid]; }
	const Family& operator[](LFID fid) const { return m_families[fid]; }

	LFID AllocateFamily();
    bool FreeFamily(LFID fid);
    bool IsEmpty() const { return m_numFamiliesUsed == 0; }
    
    FSize GetNumUsedFamilies() const { return m_numFamiliesUsed; }

    // Admin functions
    const std::vector<Family>& GetFamilies() const { return m_families; }

private:
    Processor&          m_parent;
    std::vector<Family> m_families;
    FamilyQueue         m_empty;
    FSize               m_numFamiliesUsed;
};

}
#endif

