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
    enum Type
    {
        LOCAL,
        GROUP,
    };

    struct RegInfo
    {   
        RegsNo   count;            // Number of globals, locals and shareds
        RegIndex base;             // Base address of this family's register block
        RegSize  size;             // Size of the allocated registers (could be calculated from other values)
        RegIndex last_shareds;     // Address of the last allocated thread's shareds
        RegIndex first_dependents; // Address of the dependents of the first thread in the block
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

    Type         type;           // The type of the family
    FCapability  capability;     // Capability value for security
    MemAddr      pc;             // Initial PC for newly created threads
	bool         legacy;		  // Consists of a single thread of legacy code?
    Integer      virtBlockSize;  // Virtual block size
    TSize        physBlockSize;  // Physical block size, <= Virtual block size, depending on the amount of free registers
    SInteger     start;          // Start index of the family
    SInteger     step;           // Step size of the family
	PlaceType    place;		  // Place type of this family
	bool         infinite;       // Is this an infinite family?
	union {
	    Integer  nThreads;       // Number of threads we need to allocate
		SInteger limit;		     // Limit of the family
	};
    Integer      index;          // Index of the next to be allocated thread (0, 1, 2... nThreads-1)
    LPID         parent_lpid;    // Parent core in group
    bool         hasDependency;  // Does this family use shareds?
    Dependencies dependencies;   // The dependencies for termination and cleanup
    ThreadQueue  members;        // Queue of all threads in this family
    LFID         link_next;      // The LFID of the matching family on the next CPU
    LFID         link_prev;      // The LFID of the matching family on the previous CPU
    bool         hasLastThread;  // Does this core have the last thread of this family?
    bool         prevCleanedUp;  // Last thread has been cleaned up
    
    struct
    {
        ExitCode code;           // The exit code of the family
        GPID     pid;            // The core that's synchronising
        RegIndex reg;            // The exit code register on the core
    }            sync;           // Synchronisation information
    
    TID          lastAllocated;
    bool         lastAllocatedIsLastThreadInBlock;
    TID          firstThreadInBlock;

    RegInfo      regs[NUM_REG_TYPES];    // Register information

    // Admin
    FamilyState  state;          // Family state
};

class FamilyTable : public Object
{
public:
    FamilyTable(const std::string& name, Processor& parent, const Config& config);

    typedef Family value_type;
          Family& operator[](LFID fid)       { return m_families[fid]; }
	const Family& operator[](LFID fid) const { return m_families[fid]; }

	LFID  AllocateFamily(ContextType type);
    void  FreeFamily(LFID fid, ContextType context);
    void  ReserveFamily();
    
    FSize GetNumFreeFamilies()  const;
    bool  IsEmpty()             const;
    bool  IsExclusive(LFID fid) const { return fid + 1 == m_families.size(); }
    bool  IsExclusiveUsed()     const { return m_free[CONTEXT_EXCLUSIVE] == 0; }
    
    // Admin functions
    const std::vector<Family>& GetFamilies() const { return m_families; }

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    Processor&          m_parent;
    std::vector<Family> m_families;
    FSize               m_free[NUM_CONTEXT_TYPES];
};

}
#endif

