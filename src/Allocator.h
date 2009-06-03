#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "ThreadTable.h"
#include "FamilyTable.h"
#include <queue>

class Config;

namespace Simulator
{

class Processor;
class FamilyTable;
class ThreadTable;
class RegisterFile;
class RAUnit;
class ICache;
class Network;
class Pipeline;
struct Family;
struct CreateMessage;
struct DelegateMessage;

// A list of dependencies that prevent a family from being
// terminated or cleaned up
enum FamilyDependency
{
    FAMDEP_THREAD_COUNT,        // Number of allocated threads
    FAMDEP_OUTSTANDING_READS,   // Number of outstanding memory reads
    FAMDEP_OUTSTANDING_SHAREDS, // Number of outstanding parent shareds
    FAMDEP_PREV_SYNCHRONIZED,   // Family has synchronized on the previous processor
    FAMDEP_NEXT_TERMINATED,     // Family has terminated on the next processor
	FAMDEP_ALLOCATION_DONE,     // Thread allocation is done
};

// A list of dependencies that prevent a thread from being
// terminated or cleaned up
enum ThreadDependency
{
    THREADDEP_OUTSTANDING_WRITES,   // Number of outstanding memory writes
    THREADDEP_PREV_CLEANED_UP,      // Predecessor has been cleaned up
    THREADDEP_NEXT_TERMINATED,      // Successor has terminated
    THREADDEP_TERMINATED,           // Thread has terminated
};

class Allocator : public IComponent
{
public:
    struct RegisterBases
    {
        RegIndex globals;
        RegIndex shareds;
    };

	struct AllocRequest
	{
		TID           parent;               // Thread performing the allocation (for security)
		RegIndex      reg;                  // Register that will receive the LFID
        RegisterBases bases[NUM_REG_TYPES]; // Bases of parent registers
	};

    // These are the different states in the state machine for
    // family creation
	enum CreateState
	{
		CREATE_INITIAL,             // Waiting for a family to create
		CREATE_LOADING_LINE,        // Waiting until the cache-line is loaded
		CREATE_LINE_LOADED,         // The line has been loaded
		CREATE_ACQUIRING_TOKEN,     // Requesting the token from the network
		CREATE_BROADCASTING_CREATE, // Broadcasting the create
		CREATE_ALLOCATING_REGISTERS,// Allocating register space
	};

    Allocator(Processor& parent, const std::string& name,
        FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
        LPID lpid, const Config& config);

    // Allocates the initial family consisting of a single thread on the first CPU.
    // Typically called before tha actual simulation starts.
    void AllocateInitialFamily(MemAddr pc);

    // Returns the physical register address for a logical register in a certain family.
    RegAddr GetRemoteRegisterAddress(const RemoteRegAddr& addr) const;

    // This is used in all TCB instructions to index the family table with additional checks.
	Family& GetWritableFamilyEntry(LFID fid, TID parent) const;

    /*
     * Thread management
     */
    bool ActivateThreads(const ThreadQueue& threads);   // Activates the specified threads
    bool RescheduleThread(TID tid, MemAddr pc);         // Reschedules a thread from the pipeline
    bool SuspendThread(TID tid, MemAddr pc);            // Suspends a thread at the specified PC
    bool KillThread(TID tid);                           // Kills a thread
    
    uint64_t GetTotalActiveQueueSize() const { return m_totalActiveQueueSize; }
    uint64_t GetMaxActiveQueueSize() const { return m_maxActiveQueueSize; }
    uint64_t GetMinActiveQueueSize() const { return m_minActiveQueueSize; }
    
    bool   SynchronizeFamily(LFID fid, Family& family, ExitCode code);
	Result AllocateFamily(TID parent, RegIndex reg, LFID* fid, const RegisterBases bases[]);
    bool   SanitizeFamily(Family& family, bool hasDependency);
    bool   QueueCreate(LFID fid, MemAddr address, TID parent, RegIndex exitCodeReg);
	bool   ActivateFamily(LFID fid);
	
	LFID   OnGroupCreate(const CreateMessage& msg, LFID link_next);
    bool   OnDelegatedCreate(const DelegateMessage& msg);
    
    bool   QueueActiveThreads(const ThreadQueue& threads);
    bool   QueueThreads(ThreadQueue& queue, const ThreadQueue& threads, ThreadState state);
    TID    PopActiveThread();
    
    bool   SetupFamilyPrevLink(LFID fid, LFID link_prev);
    bool   SetupFamilyNextLink(LFID fid, LFID link_next);
    
    bool   OnMemoryRead(LFID fid);
    
    bool   DecreaseFamilyDependency(LFID fid, FamilyDependency dep);
    bool   DecreaseFamilyDependency(LFID fid, Family& family, FamilyDependency dep);
    bool   IncreaseThreadDependency(          TID tid, ThreadDependency dep);
    bool   DecreaseThreadDependency(LFID fid, TID tid, ThreadDependency dep);
    
    // External events
	bool OnCachelineLoaded(CID cid);
    bool OnTokenReceived();
    bool OnRemoteThreadCompletion(LFID fid);
    bool OnRemoteThreadCleanup(LFID fid);
    bool OnRemoteSync(LFID fid, ExitCode code);

    /* Component */
    Result OnCycleReadPhase(unsigned int stateIndex);
    Result OnCycleWritePhase(unsigned int stateIndex);
    void   UpdateStatistics();

    /* Admin functions */
    FamilyTable&                 GetFamilyTable()             const { return m_familyTable; }
	TID                          GetRegisterType(LFID fid, RegAddr addr, RegClass* group) const;
    const Buffer<LFID>&          GetCreateQueue()             const { return m_creates;     }
    const Buffer<LFID>&          GetExclusiveCreateQueue()    const { return m_createsEx;   }
	CreateState                  GetCreateState()             const { return m_createState; }
	const Buffer<AllocRequest>&  GetFamilyAllocationQueue()   const { return m_allocations; }
	const FamilyQueue&           GetThreadAllocationQueue()   const { return m_alloc;       }
    const Buffer<TID>&           GetCleanupQueue()            const { return m_cleanup;     }
    LFID                         GetExclusiveFamily()         const { return m_exclusive;   }

private:
    // A queued register write
    struct RegisterWrite
    {
        RegAddr  address;   // Where to write
        RegValue value;     // What to write
    };

    // Private functions
    MemAddr CalculateTLSAddress(LFID fid, TID tid) const;
    MemSize CalculateTLSSize() const;
    
	void SetDefaultFamilyEntry(LFID fid, TID parent, const RegisterBases bases[]) const;
	void InitializeFamily(LFID fid, Family::Type type) const;
	bool AllocateRegisters(LFID fid);
	bool WriteExitCode(RegIndex reg, ExitCode code);

    bool AllocateThread(LFID fid, TID tid, bool isNewlyAllocated = true);
    bool PushCleanup(TID tid);

    // Thread and family queue manipulation
    void Push(FamilyQueue& queue, LFID fid);
    void Push(ThreadQueue& queue, TID tid, TID Thread::*link = &Thread::nextState);
    LFID Pop (FamilyQueue& queue);
    TID  Pop (ThreadQueue& queue, TID Thread::*link = &Thread::nextState);

    Processor&          m_parent;
    FamilyTable&        m_familyTable;
    ThreadTable&        m_threadTable;
    RegisterFile&       m_registerFile;
    RAUnit&             m_raunit;
    ICache&             m_icache;
    Network&            m_network;
	Pipeline&			m_pipeline;
    LPID                m_lpid;

    uint64_t             m_activeQueueSize;
    uint64_t             m_totalActiveQueueSize;
    uint64_t             m_maxActiveQueueSize;
    uint64_t             m_minActiveQueueSize;

    // Initial allocation
    LFID                 m_exclusive;  // Currently executing exclusive family
    LFID                 m_allocating; // This family we're initially allocating from
    FamilyQueue          m_alloc;      // This is the queue of families waiting for initial allocation

    // Buffers
    Buffer<LFID>          m_creates;        // Create queue
    Buffer<LFID>          m_createsEx;      // Exclusive create queue
    Buffer<RegisterWrite> m_registerWrites; // Register write queue
    Buffer<TID>           m_cleanup;        // Cleanup queue
	Buffer<AllocRequest>  m_allocations;	// Family allocation queue
	
	LFID                  m_createFID;      // Family ID of the current create
	CreateState           m_createState;	// State of the current state;
	CID                   m_createLine;		// Cache line that holds the register info

public:
    ThreadQueue			  m_activeThreads;  // Queue of the active threads
    ThreadQueue           m_readyThreads;   // Queue of the threads can be activated
};

}
#endif

