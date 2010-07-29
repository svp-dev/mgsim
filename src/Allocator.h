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
struct GroupCreateMessage;
struct RemoteCreateMessage;
struct CreateResult;
struct PlaceInfo;

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
    typedef LinkedList< TID, ThreadTable, &Thread::nextState> ThreadList;
    
	struct AllocRequest
	{
		PlaceType place;  // Type of place
		GPID      pid;    // Core that requested the allocation
		RegIndex  reg;    // Register (on that core) that will receive the FID
	};

    // These are the different states in the state machine for
    // family creation
	enum CreateState
	{
		CREATE_INITIAL,             // Waiting for a family to create
		CREATE_LOADING_LINE,        // Waiting until the cache-line is loaded
		CREATE_LINE_LOADED,         // The line has been loaded
		CREATE_ALLOCATING_REGISTERS,// Allocating register space
		CREATE_ACQUIRE_TOKEN,       // Have to request the token from the network
		CREATE_ACQUIRING_TOKEN,     // Requesting the token from the network
		CREATE_BROADCASTING_CREATE, // Broadcasting the create
		CREATE_ACTIVATING_FAMILY,   // Activating the family
		CREATE_NOTIFY,              // Notify the parent that the context is created
	};

    Allocator(const std::string& name, Processor& parent,
        FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
        PlaceInfo& place, LPID lpid, const Config& config);

    // Allocates the initial family consisting of a single thread on the first CPU.
    // Typically called before tha actual simulation starts.
    void AllocateInitialFamily(MemAddr pc, bool legacy);

    // Returns the physical register address for a logical register in a certain family.
    RegAddr GetRemoteRegisterAddress(const RemoteRegAddr& addr) const;

    Family& GetFamilyChecked(LFID fid, FCapability capability) const;

    /*
     * Thread management
     */
    bool ActivateThreads(const ThreadQueue& threads);   // Activates the specified threads
    bool RescheduleThread(TID tid, MemAddr pc);         // Reschedules a thread from the pipeline
    bool SuspendThread(TID tid, MemAddr pc);            // Suspends a thread at the specified PC
    bool KillThread(TID tid);                           // Kills a thread
    
    bool   SynchronizeFamily(LFID fid, Family& family);
	Result AllocateFamily(const PlaceID& place, GPID src, RegIndex reg, FID* fid);
    void   SanitizeFamily(Family& family, bool hasDependency);
    bool   QueueCreate(const FID& fid, MemAddr address, RegIndex completion);
	bool   ActivateFamily(LFID fid);
	
	bool   OnCreateCompleted(LFID fid, RegIndex completion);
	LFID   OnGroupCreate(const GroupCreateMessage& msg);
    bool   OnDelegatedCreate(const RemoteCreateMessage& msg, GPID remote_pid);
    
    bool   QueueActiveThreads(const ThreadQueue& threads);
    bool   QueueThreads(ThreadList& list, const ThreadQueue& threads, ThreadState state);
    
    bool   OnMemoryRead(LFID fid);
    
    bool   DecreaseFamilyDependency(LFID fid, FamilyDependency dep);
    bool   DecreaseFamilyDependency(LFID fid, Family& family, FamilyDependency dep);
    bool   IncreaseThreadDependency(TID tid, ThreadDependency dep);
    bool   DecreaseThreadDependency(TID tid, ThreadDependency dep);
    
    TID    PopActiveThread();
    
    // External events
	bool OnCachelineLoaded(CID cid);
    bool OnTokenReceived();
    bool OnRemoteSync(LFID fid, FCapability capability, GPID remote_pid, RegIndex remote_reg);
    bool OnRemoteThreadCleanup(LFID fid);
    void ReserveContext();

    // Helpers
	TID     GetRegisterType(LFID fid, RegAddr addr, RegClass* group) const;
    
	// Debugging
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    // A queued integer register write
    struct RegisterWrite
    {
        RegIndex address;   // Where to write
        Integer  value;     // What to write
    };

    /// Information for buffered creates
    struct CreateInfo
    {
        LFID     fid;        ///< Family index to start create process.
        GPID     pid;        ///< Core to send create completion to.
        RegIndex completion; ///< Register to write on core when create completed.
    };

	FCapability InitializeFamily(LFID fid, PlaceType place) const;
	void ReinitializeFamily(LFID fid, Family::Type type) const;
	bool AllocateRegisters(LFID fid, ContextType type);

    bool AllocateThread(LFID fid, TID tid, bool isNewlyAllocated = true);
    bool PushCleanup(TID tid);
    
    bool IsContextAvailable() const;
    void UpdateContextAvailability();

    // Thread queue manipulation
    void Push(ThreadQueue& queue, TID tid, TID Thread::*link = &Thread::nextState);
    TID  Pop (ThreadQueue& queue, TID Thread::*link = &Thread::nextState);

    Processor&    m_parent;
    FamilyTable&  m_familyTable;
    ThreadTable&  m_threadTable;
    RegisterFile& m_registerFile;
    RAUnit&       m_raunit;
    ICache&       m_icache;
    Network&      m_network;
	Pipeline&	  m_pipeline;
	PlaceInfo&    m_place;
    LPID          m_lpid;
    
    Buffer<LFID>          m_alloc;          ///< This is the queue of families waiting for initial allocation
    Buffer<CreateInfo>    m_creates;        ///< Create queue
    Buffer<RegisterWrite> m_registerWrites; ///< Register write queue
    Buffer<TID>           m_cleanup;        ///< Cleanup queue
	Buffer<AllocRequest>  m_allocations;	///< Family allocation queue
	Buffer<AllocRequest>  m_allocationsEx;  ///< Exclusive family allocation queue
	CreateState           m_createState;	///< State of the current state;
	CID                   m_createLine;	   	///< Cache line that holds the register info
    ThreadList            m_readyThreads1;  ///< Queue of the threads can be activated; from the pipeline
    ThreadList            m_readyThreads2;  ///< Queue of the threads can be activated; from the rest
    
    Result DoThreadAllocate();
    Result DoFamilyAllocate();
    Result DoFamilyCreate();
    Result DoThreadActivation();
    Result DoRegWrites();

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
    Process p_RegWrites;

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

