#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "ThreadTable.h"
#include "FamilyTable.h"
#include "storage.h"
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
struct RemoteMessage;
struct LinkMessage;

// A list of dependencies that prevent a family from being
// terminated or cleaned up
enum FamilyDependency
{
    FAMDEP_THREAD_COUNT,        // Number of allocated threads
    FAMDEP_OUTSTANDING_READS,   // Number of outstanding memory reads
    FAMDEP_PREV_SYNCHRONIZED,   // Family has synchronized on the previous processor
    FAMDEP_DETACHED,            // Family has been detached
	FAMDEP_ALLOCATION_DONE,     // Thread allocation is done
};

// A list of dependencies that prevent a thread from being
// terminated or cleaned up
enum ThreadDependency
{
    THREADDEP_OUTSTANDING_WRITES,   // Number of outstanding memory writes
    THREADDEP_PREV_CLEANED_UP,      // Predecessor has been cleaned up
    THREADDEP_TERMINATED,           // Thread has terminated
};

class Allocator : public Object
{
public:
    typedef LinkedList< TID, ThreadTable, &Thread::next> ThreadList;
    
	struct AllocRequest
	{
	    LFID      first_fid;      ///< FID of the family on the first core
	    LFID      prev_fid;       ///< FID of the family on the previous core
		PSize     placeSize;      ///< Number of cores to allocate on
		bool      exact;          ///< Allocate exactly placeSize cores
		PID       completion_pid; ///< Core that requested the allocation
		RegIndex  completion_reg; ///< Register (on that core) that will receive the FID
	};

    // These are the different states in the state machine for
    // family creation
	enum CreateState
	{
		CREATE_INITIAL,             // Waiting for a family to create
		CREATE_LOADING_LINE,        // Waiting until the cache-line is loaded
		CREATE_LINE_LOADED,         // The line has been loaded
		CREATE_RESTRICTING,         // Check family property and restrict if necessary
		CREATE_ALLOCATING_REGISTERS,// Allocating register space
		CREATE_BROADCASTING_CREATE, // Broadcasting the create
		CREATE_ACTIVATING_FAMILY,   // Activating the family
		CREATE_NOTIFY,              // Notifying creator
	};

    Allocator(const std::string& name, Processor& parent, Clock& clock,
        FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
        const Config& config);

    // Allocates the initial family consisting of a single thread on the first CPU.
    // Typically called before tha actual simulation starts.
    void AllocateInitialFamily(MemAddr pc, bool legacy);

    /// Allocates a contexts and sets the family's 'link' field to prev_fid
    LFID AllocateContext(ContextType type, LFID prev_fid, PSize placeSize);

    // Returns the physical register address for a logical register in a certain family.
    RegAddr GetRemoteRegisterAddress(LFID fid, RemoteRegType kind, const RegAddr& addr) const;

    Family& GetFamilyChecked(LFID fid, FCapability capability) const;

    //
    // Thread management
    //
    bool ActivateThreads(const ThreadQueue& threads);   // Activates the specified threads
    bool RescheduleThread(TID tid, MemAddr pc);         // Reschedules a thread from the pipeline
    bool SuspendThread(TID tid, MemAddr pc);            // Suspends a thread at the specified PC
    bool KillThread(TID tid);                           // Kills a thread
    
    bool QueueFamilyAllocation(const RemoteMessage& msg, PID src);
    bool QueueFamilyAllocation(const LinkMessage& msg);
	bool ActivateFamily(LFID fid);

	FCapability InitializeFamily(LFID fid) const;
	void ReleaseContext(LFID fid);
	
    bool QueueCreate(const RemoteMessage& msg, PID src);
    bool QueueCreate(const LinkMessage& msg);    
    bool QueueActiveThreads(const ThreadQueue& threads);
    bool QueueThreads(ThreadList& list, const ThreadQueue& threads, ThreadState state);
    
    bool OnCachelineLoaded(CID cid);
    bool OnMemoryRead(LFID fid);
    
    bool DecreaseFamilyDependency(LFID fid, FamilyDependency dep);
    bool DecreaseFamilyDependency(LFID fid, Family& family, FamilyDependency dep);
    bool IncreaseThreadDependency(TID tid, ThreadDependency dep);
    bool DecreaseThreadDependency(TID tid, ThreadDependency dep);
    
    TID PopActiveThread();
    
    // Helpers
	TID  GetRegisterType(LFID fid, RegAddr addr, RegClass* group) const;
    
	// Debugging
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    struct CreateInfo
    {
        LFID     fid;
        PID      completion_pid;
        RegIndex completion_reg;
    };
    
    // A queued integer register write
    struct RegisterWrite
    {
        RegIndex address;   // Where to write
        Integer  value;     // What to write
    };

    Integer CalculateThreadCount(const Family& family);
    void    CalculateDistribution(Family& family, Integer nThreads, PSize numCores);
	bool    AllocateRegisters(LFID fid, ContextType type);
    bool    AllocateThread(LFID fid, TID tid, bool isNewlyAllocated = true);
    bool    PushCleanup(TID tid);   
    bool    IsContextAvailable(ContextType type) const;

    // Thread queue manipulation
    void Push(ThreadQueue& queue, TID tid);
    TID  Pop (ThreadQueue& queue);

    Processor&    m_parent;
    FamilyTable&  m_familyTable;
    ThreadTable&  m_threadTable;
    RegisterFile& m_registerFile;
    RAUnit&       m_raunit;
    ICache&       m_icache;
    Network&      m_network;
	Pipeline&	  m_pipeline;
    
    Buffer<LFID>          m_alloc;                   ///< This is the queue of families waiting for initial allocation
    Buffer<CreateInfo>    m_creates;                 ///< Create queue
    Buffer<TID>           m_cleanup;                 ///< Cleanup queue
	CreateState           m_createState;	         ///< State of the current state;
	CID                   m_createLine;	   	         ///< Cache line that holds the register info
    ThreadList            m_readyThreads1;           ///< Queue of the threads can be activated; from the pipeline
    ThreadList            m_readyThreads2;           ///< Queue of the threads can be activated; from the rest
    ThreadList*           m_prevReadyList;           ///< Which ready list was used last cycle. For round-robin prioritization.

    // The family allocation request queues
	Buffer<AllocRequest>  m_allocRequestsSuspend;	 ///< Non-exclusive requests that want to suspend.
	Buffer<AllocRequest>  m_allocRequestsNoSuspend;	 ///< Non-exclusive requests that do not want to suspend.
	Buffer<AllocRequest>  m_allocRequestsExclusive;  ///< Exclusive requests.
    
    Result DoThreadAllocate();
    Result DoFamilyAllocate();
    Result DoFamilyCreate();
    Result DoThreadActivation();

    // Statistics
    BufferSize m_maxallocex;
    BufferSize m_totalallocex;
    CycleNo    m_lastcycle;
    BufferSize m_curallocex;
    void       UpdateStats();

public:
    // Processes
    Process p_ThreadAllocate;
    Process p_FamilyAllocate;
    Process p_FamilyCreate;
    Process p_ThreadActivation;

    ArbitratedService<>   p_allocation;     ///< Arbitrator for FamilyTable::AllocateFamily
    ArbitratedService<>   p_alloc;          ///< Arbitrator for m_alloc
    ArbitratedService<>   p_readyThreads;   ///< Arbitrator for m_readyThreads2
    ArbitratedService<>   p_activeThreads;  ///< Arbitrator for m_activeThreads
    ThreadList            m_activeThreads;  ///< Queue of the active threads

    // Statistics
    BufferSize GetTotalAllocatedEx() { UpdateStats(); return m_totalallocex; }
    BufferSize GetMaxAllocatedEx() const { return m_maxallocex; }
};

}
#endif

