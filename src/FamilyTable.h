#ifndef FAMILYTABLE_H
#define FAMILYTABLE_H

#include "ports.h"
#include <queue>

class Config;

namespace Simulator
{

class Processor;

struct Family
{
    struct RegInfo
    {   
        RegsNo   count;            // Number of globals, locals and shareds
        RegIndex base;             // Base address of this family's register block
        RegSize  size;             // Size of the allocated registers (could be calculated from other values)
        RegIndex last_shareds;     // Address of the last allocated thread's shareds
        //RegIndex first_dependents; // Address of the dependents of the first thread in the block
    };
    
    // Groups all dependencies that need to be resolved before termination and/or cleanup
    struct Dependencies
    {
        /*
         All threads in the family must have been allocated before the family is done
        */
        bool allocationDone;
        
        /*
         The parent threads needs to be notified of the termination of the
         family. This is done by having each family notify the family on the
         next core once itself and its predecessor has terminated. This flag
         indicates the family has terminated on all preceding cores.
         Eventually this will cause this flag to be set on the family on the
         parent core.
        */
        bool prevSynchronized;
        
        /*
         After synchronizing on the family's termination, the parent thread
         can still read the final shareds back from the last thread's context.
         Therefore, we cannot cleanup a family until the parent has
         explicitely detached from it.
        */
        bool detached;
		
        /*
         All allocated threads (0 <= allocated <= physBlockSize) must have
         been cleaned up before the family has terminated.
        */
        TSize numThreadsAllocated;
        
        /*
         FIXME:
         Maybe this can be generalized to a thread not being cleaned up
         before all pending operations made by it have completed. Then
         this dependency becomes implicit in the numThreadsAllocated
         dependency.
        */
        unsigned int numPendingReads;
	};

    PSize        placeSize;      // Number of cores this family wanted to run on.
    PSize        numCores;       // Number of cores this family is actually running on (1 <= numCores <= placeSize).
    FCapability  capability;     // Capability value for security
    MemAddr      pc;             // Initial PC for newly created threads
	bool         legacy;		 // Consists of a single thread of legacy code?
    TSize        physBlockSize;  // Physical block size, <= Virtual block size, depending on the amount of free registers
    SInteger     start;          // Start index of the family (on this core)
    SInteger     step;           // Step size of the family
	union {
		SInteger limit;		     // Limit of the family
	    Integer  nThreads;       // Number of threads we still need to run (on this core)
	};
    bool         hasShareds;     // Does this family use shareds?
    Dependencies dependencies;   // The dependencies for termination and cleanup
    LFID         first_lfid;     // The LFID of the matching family on the first CPU (used for break)
    LFID         link;           // The LFID of the matching family on the next CPU (prev during allocate)
    bool         prevCleanedUp;  // Last thread has been cleaned up
    
    struct
    {
        bool     done;           // Whether the family is done or not
        PID      pid;            // The core that's synchronising
        RegIndex reg;            // The exit code register on the core
    }            sync;           // Synchronisation information
    
    TID          lastAllocated;

    RegInfo      regs[NUM_REG_TYPES];    // Register information

    // Admin
    FamilyState  state;          // Family state
};

class FamilyTable : public Object
{
public:
    FamilyTable(const std::string& name, Processor& parent, Clock& clock, const Config& config);

    FSize GetNumFamilies() const { return m_families.size(); }

    typedef Family value_type;
          Family& operator[](LFID fid)       { return m_families[fid]; }
	const Family& operator[](LFID fid) const { return m_families[fid]; }

	LFID  AllocateFamily(ContextType type);
    void  FreeFamily(LFID fid, ContextType context);
    
    FSize GetNumFreeFamilies(ContextType type) const;
    bool  IsEmpty()             const;
    bool  IsExclusive(LFID fid) const { return fid + 1 == m_families.size(); }
    bool  IsExclusiveUsed()     const { return m_free[CONTEXT_EXCLUSIVE] == 0; }
    
    // Admin functions
    const std::vector<Family>& GetFamilies() const { return m_families; }

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

    // Stats
    FSize GetTotalAllocated() { UpdateStats(); return m_totalalloc; }
    TSize GetMaxAllocated() const { return m_maxalloc; }

private:
    Processor&          m_parent;
    std::vector<Family> m_families;
    FSize               m_free[NUM_CONTEXT_TYPES];
    FSize               m_totalalloc;
    FSize               m_maxalloc;
    CycleNo             m_lastcycle;
    FSize               m_curalloc;

    void UpdateStats();    
    void CheckStateSanity() const;
};

}
#endif

