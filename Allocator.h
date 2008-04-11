#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "functions.h"
#include "Memory.h"
#include "ThreadTable.h"
#include <queue>

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

// A list of dependencies that prevent a family from being
// terminated or cleaned up
enum FamilyDependency
{
    FAMDEP_THREAD_COUNT,
    FAMDEP_OUTSTANDING_READS,
    FAMDEP_OUTSTANDING_WRITES,
    FAMDEP_OUTSTANDING_SHAREDS,
    FAMDEP_THREADS_RUNNING,
	FAMDEP_THREADS_QUEUED,
    FAMDEP_PREV_TERMINATED,
	FAMDEP_CREATE_COMPLETED,
};

class Allocator : public IComponent
{
public:
	struct Config
	{
		BufferSize localCreatesSize;
		BufferSize remoteCreatesSize;
		BufferSize cleanupSize;
	};

	struct AllocRequest
	{
		TID      parent; // Thread performing the allocation (for security)
		RegIndex reg;	 // Register that will receive the LFID
	};

	enum CreateState
	{
		CREATE_INITIAL,
		CREATE_LOADING_LINE,
		CREATE_LINE_LOADED,
		CREATE_GETTING_TOKEN,
		CREATE_HAS_TOKEN,
		CREATE_RESERVING_FAMILY,
		CREATE_BROADCASTING_CREATE,
		CREATE_ALLOCATING_REGISTERS,
	};

    Allocator(Processor& parent, const std::string& name,
        FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
        PSize procNo, const Config& config);

    void allocateInitialFamily(MemAddr pc, bool legacy);
    bool idle()   const;

    // These implement register mappings
    RegAddr  getSharedAddress(SharedType stype, GFID fid, RegAddr addr) const;

	Family& GetWritableFamilyEntry(LFID fid, TID parent) const;

    // Public functions
    bool activateThread(TID tid, const IComponent& component, MemAddr* pPC = NULL, LFID fid = INVALID_LFID);
    bool killThread(TID tid);
    bool killFamily(LFID fid, ExitCode code, RegValue value);
	Result AllocateFamily(TID parent, RegIndex reg, LFID* fid);
	LFID AllocateFamily(const CreateMessage& msg);
	bool ActivateFamily(LFID fid);
    bool queueCreate(LFID fid, MemAddr address, TID parent, RegAddr exitCodeReg);
    bool markNextKilled(TID tid);
    bool markPrevCleanedUp(TID tid);
    bool increaseFamilyDependency(LFID fid, FamilyDependency dep);
    bool decreaseFamilyDependency(LFID fid, FamilyDependency dep);
    bool queueActiveThreads(TID first, TID last);

    // External events
	bool onCachelineLoaded(CID cid);
	bool onReservationComplete();
    bool onTokenReceived();
    bool onRemoteThreadCompletion(LFID fid);
    bool onRemoteThreadCleanup(LFID fid);

    /* Component */
    Result onCycleReadPhase(int stateIndex);
    Result onCycleWritePhase(int stateIndex);

    /* Admin functions */
	TID GetRegisterType(LFID fid, RegAddr addr, RegGroup* group) const;
    const Buffer<LFID>&          getCreateQueue()     const { return m_creates;     }
	CreateState                  GetCreateState()     const { return m_createState; }
	const Buffer<AllocRequest>&  GetAllocationQueue() const { return m_allocations; }
    const Buffer<TID>&           getCleanupQueue()    const { return m_cleanup;     }

    //
    // Functions
    //
    ArbitratedWriteFunction     p_cleanup;

private:
    // A queued register write
    struct RegisterWrite
    {
        RegAddr  address;   // Where to write
        RegValue value;     // What to write
    };

    // Private functions
	void SetDefaultFamilyEntry(LFID fid, TID parent) const;
	void InitializeFamily(LFID fid) const;
	bool AllocateRegisters(LFID fid);

    bool allocateThread(LFID fid, TID tid, bool isNewlyAllocated = true);
    bool pushCleanup(TID tid);

    // Thread and family queue manipulation
    void push(FamilyQueue& queue, LFID fid);
    void push(ThreadQueue& queue, TID tid, TID Thread::*link = &Thread::nextState);
    LFID  pop (FamilyQueue& queue);
    TID  pop (ThreadQueue& queue, TID Thread::*link = &Thread::nextState);

    Processor&          m_parent;
    FamilyTable&        m_familyTable;
    ThreadTable&        m_threadTable;
    RegisterFile&       m_registerFile;
    RAUnit&             m_raunit;
    ICache&             m_icache;
    Network&            m_network;
	Pipeline&			m_pipeline;
    PSize               m_procNo;

    // Initial allocation
    LFID                 m_allocating;   // This family we're initially allocating from
    FamilyQueue          m_alloc;        // This is the queue of families waiting for initial allocation

    // Buffers
    Buffer<LFID>          m_creates;   // Local create queue
    Buffer<RegisterWrite> m_registerWrites; // Register write queue
    Buffer<TID>           m_cleanup;        // Cleanup queue
	Buffer<AllocRequest>  m_allocations;	// Family allocation queue
	CreateState           m_createState;	// State of the current state;
	CID                   m_createLine;		// Cache line that holds the register info

public:
    ThreadQueue			  m_activeThreads;  // Queue of the active threads
};

}
#endif

