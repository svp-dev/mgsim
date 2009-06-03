#include "Allocator.h"
#include "Processor.h"
#include "Pipeline.h"
#include "Network.h"
#include "config.h"
#include <cassert>
#include <cmath>
using namespace Simulator;
using namespace std;

RegAddr Allocator::GetRemoteRegisterAddress(const RemoteRegAddr& addr) const
{
	const Family&          family = m_familyTable[addr.fid];
    const Family::RegInfo& regs   = family.regs[addr.reg.type];

    assert(family.state != FST_EMPTY);
    
    RegIndex base = INVALID_REG_INDEX;
    switch (addr.type)
    {
        case RRT_GLOBAL:
            assert(family.type == Family::GROUP || family.type == Family::DELEGATED || family.parent.gpid != INVALID_GPID);
            base = regs.globals;
            break;
            
        case RRT_PARENT_SHARED:
            // This should only be used if the parent thread is on this CPU
            assert(family.type == Family::GROUP || family.type == Family::DELEGATED);
            assert(family.parent.lpid == m_lpid);

            // Return the shared address in the parent thread
            if (family.parent.tid != INVALID_TID)
            {
                base = regs.shareds;
            }
            break;

        case RRT_FIRST_DEPENDENT:
            // Return the dependent address for the first thread in the block
            assert(family.type == Family::GROUP || family.parent.gpid != INVALID_GPID);
            if (family.firstThreadInBlock != INVALID_TID)
            {
        		// Get base of remote dependency block
                base = regs.base + regs.size - regs.count.shareds;
            }
            break;
            
        case RRT_LAST_SHARED:
            // Return the shared address for the last thread in the block
            assert(family.type == Family::GROUP);
            if (family.lastThreadInBlock != INVALID_TID)
            {
        		// Get base of last thread's shareds
                base = m_threadTable[family.lastThreadInBlock].regs[addr.reg.type].base;
            }
            break;
    }
    return MAKE_REGADDR(addr.reg.type, (base != INVALID_REG_INDEX) ? base + addr.reg.index : INVALID_REG_INDEX);
}

// Administrative function for getting a register's type and thread mapping
TID Allocator::GetRegisterType(LFID fid, RegAddr addr, RegClass* group) const
{
    const Family& family = m_familyTable[fid];
    const Family::RegInfo& regs = family.regs[addr.type];

	if (addr.index >= regs.globals && addr.index < regs.globals + regs.count.globals)
	{
		// It's a global
		*group = RC_GLOBAL;
		return INVALID_TID;
	}

    if (regs.shareds >= regs.base && regs.shareds < regs.base + regs.size)
    {
	    if (addr.index >= regs.base + regs.size - regs.count.shareds && addr.index < regs.base + regs.size)
	    {
    		// It's a remote dependency
		    *group = RC_DEPENDENT;
		    return INVALID_TID;
	    }
    }
    
    if (regs.count.locals + regs.count.shareds > 0)
    {
        // It's a local or shared; check which thread it belongs to
    	RegIndex index = (addr.index - regs.base) % (regs.count.locals + regs.count.shareds);
    	RegIndex  base = addr.index - index;

	    for (TID cur = family.members.head; cur != INVALID_TID;)
	    {
    		const Thread& thread = m_threadTable[cur];
		    if (thread.regs[addr.type].base == base)
		    {
    			*group = (index < regs.count.shareds) ? RC_SHARED : RC_LOCAL;
			    return cur;
		    }
		    cur = thread.nextMember;
	    }
	}
	
	// Belongs to no thread
    *group = RC_LOCAL;
    return INVALID_TID;
}

bool Allocator::QueueActiveThreads(const ThreadQueue& threads)
{
    return QueueThreads(m_activeThreads, threads, TST_ACTIVE);
}

//
// Adds the list of threads to the family's active queue.
// There is assumed to be a linked list between the threads.head
// and threads.tail thread by Thread::nextState.
//
bool Allocator::QueueThreads(ThreadQueue& queue, const ThreadQueue& threads, ThreadState state)
{
    assert(threads.head != INVALID_TID);
    assert(threads.tail != INVALID_TID);

    COMMIT
    {
        // Append the waiting queue to the queue
        if (queue.head != INVALID_TID) {
            m_threadTable[queue.tail].nextState = threads.head;
        } else {
            queue.head = threads.head;
        }
        queue.tail = threads.tail;

		m_threadTable[queue.tail].nextState = INVALID_TID;

        // Admin: Mark the threads as active
        for (TID cur = threads.head; cur != INVALID_TID; cur = m_threadTable[cur].nextState)
        {
            m_threadTable[cur].state = state;
            if (state == TST_ACTIVE)
            {
                m_activeQueueSize++;
            }
        }
    }
    return true;
}

TID Allocator::PopActiveThread()
{
    assert(m_activeQueueSize > 0);

    Thread& thread = m_threadTable[m_activeThreads.head];
    m_activeThreads.head = thread.nextState;
    m_activeQueueSize--;
    return m_activeThreads.head;
}

//
// This is called by the Network to indicate that the first thread in
// a block has finished on the neighbouring processor.
//
bool Allocator::OnRemoteThreadCompletion(LFID fid)
{
    Family& family = m_familyTable[fid];

    // The last thread in a family must exist for this notification
    assert(family.lastThreadInBlock != INVALID_TID);
    if (!DecreaseThreadDependency(fid, family.lastThreadInBlock, THREADDEP_NEXT_TERMINATED))
    {
        return false;
    }

    COMMIT{ family.lastThreadInBlock = INVALID_TID; }
    return true;
}

bool Allocator::OnRemoteThreadCleanup(LFID fid)
{
    Family& family = m_familyTable[fid];

    // The first thread in a block must exist for this notification
    if (family.state == FST_EMPTY || family.firstThreadInBlock == INVALID_TID)
    {
        return false;
    }
    assert(family.firstThreadInBlock != INVALID_TID);

    if (!DecreaseThreadDependency(fid, family.firstThreadInBlock, THREADDEP_PREV_CLEANED_UP))
    {
        return false;
    }
    
    COMMIT{ family.firstThreadInBlock = INVALID_TID; }
    return true;
}

//
// This is called by various components (RegisterFile, Pipeline, ...) to
// add the threads to the ready queue.
//
bool Allocator::ActivateThreads(const ThreadQueue& threads)
{
    if (!QueueThreads(m_readyThreads, threads, TST_READY))
    {
        DeadlockWrite("Unable to enqueue threads to the Ready Queue");
        return false;
    }
    
    return true;
}

//
// Kill the thread.
// This means it will be marked as killed and pushed to the
// cleanup queue should it also be marked for cleanup.
// Called from the pipeline
//
bool Allocator::KillThread(TID tid)
{
    Thread& thread = m_threadTable[tid];
    assert(thread.state == TST_RUNNING);

    if (!m_icache.ReleaseCacheLine(thread.cid))
    {
        return false;
    }

    Family& family = m_familyTable[thread.family];
    if (family.hasDependency && (family.type == Family::GROUP || family.physBlockSize > 1))
    {
        // Mark 'next thread kill' on the previous thread
        if (thread.prevInBlock != INVALID_TID)
        {
            // Signal our termination to our predecessor
            if (!DecreaseThreadDependency(thread.family, thread.prevInBlock, THREADDEP_NEXT_TERMINATED))
            {
                return false;
            }
        }
        else if (!thread.isFirstThreadInFamily)
        {
            // Send remote notification 
            if (!m_network.SendThreadCompletion(family.link_prev))
            {
                return false;
            }
        }
    }

    
    if (thread.isLastThreadInFamily && family.dependencies.numPendingShareds > 0)
    {
        // The last thread didn't write all its shareds
        OutputWrite("Thread T%u (F%u) did not write all its shareds", tid, thread.family);
    }

    // Mark the thread as killed
    if (!DecreaseThreadDependency(thread.family, tid, THREADDEP_TERMINATED))
    {
        return false;
    }

    DebugSimWrite("Killed thread T%u", tid);

    COMMIT
    {
        thread.cid    = INVALID_CID;
        thread.state  = TST_KILLED;
    }
    return true;
}

//
// Reschedules the thread at the specified PC.
// The component is used to determine port arbitration priorities.
// Called from the pipeline.
//
bool Allocator::RescheduleThread(TID tid, MemAddr pc)
{
    Thread& thread = m_threadTable[tid];
    assert(thread.state == TST_RUNNING);
    
    // Save the (possibly overriden) program counter
    COMMIT
    {
        thread.pc = pc;
        thread.nextState = INVALID_TID;
    }
    
    if (!m_icache.ReleaseCacheLine(thread.cid))
    {
        return false;
    }

    // The thread can be added to the ready queue
    ThreadQueue tq = {tid, tid};
    if (!QueueThreads(m_readyThreads, tq, TST_READY))
    {
        DeadlockWrite("Unable to enqueue T%u in F%u to the Ready Queue", tid, thread.family);
        return false;
    }

    DebugSimWrite("Rescheduling thread T%u in F%u to 0x%llx", tid, thread.family, (unsigned long long)pc );
    return true;
}

//
// Suspends the thread at the specific PC.
// Called from the pipeline.
//
bool Allocator::SuspendThread(TID tid, MemAddr pc)
{
    Thread& thread = m_threadTable[tid];
    assert(thread.state == TST_RUNNING);

    if (!m_icache.ReleaseCacheLine(thread.cid))
    {
        return false;
    }

    COMMIT
    {
        thread.cid   = INVALID_CID;
        thread.pc    = pc;
        thread.state = TST_SUSPENDED;
    }
    return true;
}

MemAddr Allocator::CalculateTLSAddress(LFID /* fid */, TID tid) const
{
    // 1 bit for TLS/GS
    // P bits for CPU
    // T bits for TID   
    unsigned int P = (unsigned int)ceil(log2(m_parent.GetGridSize()));
    unsigned int T = (unsigned int)ceil(log2(m_threadTable.GetNumThreads()));    
    assert(sizeof(MemAddr) * 8 > P + T + 1);
    
    unsigned int Ls  = sizeof(MemAddr) * 8 - 1;
    unsigned int Ps  = Ls - P;
    unsigned int Ts  = Ps - T;
    
    return (static_cast<MemAddr>(1)                 << Ls) |
           (static_cast<MemAddr>(m_parent.GetPID()) << Ps) |
           (static_cast<MemAddr>(tid)               << Ts);
}

MemSize Allocator::CalculateTLSSize() const
{
    unsigned int P = (unsigned int)ceil(log2(m_parent.GetGridSize()));
    unsigned int T = (unsigned int)ceil(log2(m_threadTable.GetNumThreads()));    
    assert(sizeof(MemAddr) * 8 > P + T + 1);
    
    return static_cast<MemSize>(1) << (sizeof(MemSize) * 8 - (1 + P + T));
}

//
// Allocates a new thread to the specified thread slot.
// @isNew indicates if this a new thread for the family, or a recycled
// cleaned up one
//
bool Allocator::AllocateThread(LFID fid, TID tid, bool isNewlyAllocated)
{
    // Work on a copy unless we're committing
    Family tmp_family; Family* family = &tmp_family;
    Thread tmp_thread; Thread* thread = &tmp_thread;
    if (IsCommitting())
    {
        family = &m_familyTable[fid];
        thread = &m_threadTable[tid];
    }
    else
    {
        tmp_family = m_familyTable[fid];
        tmp_thread = m_threadTable[tid];
    }
    
    // Initialize thread
    thread->isFirstThreadInFamily = (family->index == 0);
    thread->isLastThreadInFamily  = (!family->infinite != 0 && family->index + 1 == family->nThreads);
	thread->isLastThreadInBlock   = (family->type == Family::GROUP && (family->index % family->virtBlockSize) == family->virtBlockSize - 1);
    thread->cid                   = INVALID_CID;
    thread->pc                    = family->pc;
    thread->family                = fid;
    thread->index                 = family->index;
    thread->prevInBlock           = INVALID_TID;
    thread->nextInBlock           = INVALID_TID;
	thread->waitingForWrites      = false;
	thread->nextState             = INVALID_TID;

    // Initialize dependencies:
    // These dependencies only hold for non-border threads in dependent families that are global or have more than one thread running.
    // So we already set them in the other cases.
	thread->dependencies.nextKilled       = !family->hasDependency || thread->isLastThreadInFamily  || (family->type == Family::LOCAL && family->physBlockSize == 1);
    thread->dependencies.prevCleanedUp    = !family->hasDependency || thread->isFirstThreadInFamily || (family->type == Family::LOCAL && family->physBlockSize == 1);
    thread->dependencies.killed           = false;
    thread->dependencies.numPendingWrites = 0;

    Thread* predecessor = NULL;
    if ((family->type == Family::LOCAL || family->index % family->virtBlockSize != 0) && family->physBlockSize > 1)
    {
        thread->prevInBlock = family->lastAllocated;
        if (thread->prevInBlock != INVALID_TID)
        {
            predecessor = &m_threadTable[thread->prevInBlock];
            COMMIT{ predecessor->nextInBlock = tid; }
        }
    }
	else if (family->physBlockSize == 1 && family->type == Family::LOCAL && thread->index > 0)
	{
		// We're not the first thread, in a family executing on one CPU, with a block size of one.
		// We are our own predecessor in terms of shareds.
		predecessor = &m_threadTable[tid];
	}

    // Set the register information for the new thread
    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
        if (isNewlyAllocated)
        {
            thread->regs[i].base = family->regs[i].base + (RegIndex)family->dependencies.numThreadsAllocated * (family->regs[i].count.locals + family->regs[i].count.shareds);
        }

        thread->regs[i].producer = (predecessor != NULL)
			? predecessor->regs[i].base		// Producer runs on the same processor (predecessor)
			: (family->type == Family::LOCAL
			   ? family->regs[i].shareds	// Producer runs on the same processor (parent)
			   : INVALID_REG_INDEX);		// Producer runs on the previous processor (parent or predecessor)

        if (family->regs[i].count.shareds > 0)
        {
            // Clear thread registers
            if (family->type == Family::GROUP || family->physBlockSize > 1)
            {
                // Clear the thread's shareds
                if (!m_registerFile.Clear(MAKE_REGADDR(i, thread->regs[i].base), family->regs[i].count.shareds))
                {
                    DeadlockWrite("Unable to clear the thread's shareds");
                    return false;
                }
            }

            if (family->type == Family::GROUP && thread->prevInBlock == INVALID_TID)
            {
                // Clear the thread's remote cache
                if (!m_registerFile.Clear(MAKE_REGADDR(i, family->regs[i].base + family->regs[i].size - family->regs[i].count.shareds), family->regs[i].count.shareds))
                {
                    DeadlockWrite("Unable to clear the thread's remote dependents");
                    return false;
                }
            }
        }
    }

    const MemAddr tls_base = CalculateTLSAddress(fid, tid);
    const MemSize tls_size = CalculateTLSSize();
    COMMIT
    {
        // Reserve the memory (commits on use)
        m_parent.ReserveTLS(tls_base, tls_size);
    }

    // Write L0 and L1 to the register file
    if (family->regs[RT_INTEGER].count.locals > 0)
    {
        RegIndex L0   = thread->regs[RT_INTEGER].base + family->regs[RT_INTEGER].count.shareds;
        RegAddr  addr = MAKE_REGADDR(RT_INTEGER, L0);
        RegValue data;
        data.m_state   = RST_FULL;
        data.m_integer = family->start + family->index * family->step;

        if (!m_registerFile.p_asyncW.Write(addr))
        {
            DeadlockWrite("Unable to acquire Register File port");
            return false;
        }

        if (!m_registerFile.WriteRegister(addr, data, false))
        {
            DeadlockWrite("Unable to write L0 register");
            return false;
        }

        if (family->regs[RT_INTEGER].count.locals > 1)
        {
            RegAddr  addr = MAKE_REGADDR(RT_INTEGER, L0 + 1);
            RegValue data;
            data.m_state   = RST_FULL;
            data.m_integer = tls_base + tls_size;   // TLS ptr starts at the top

            if (!m_registerFile.WriteRegister(addr, data, false))
            {
                DeadlockWrite("Unable to write L1 register");
                return false;
            }
        }
    }

    bool isFirstAllocatedThread = false;
    if (isNewlyAllocated)
    {
        assert(family->dependencies.numThreadsAllocated < family->physBlockSize);

        isFirstAllocatedThread = (family->members.head == INVALID_TID);
        
		// Add the thread to the family's member queue
        thread->nextMember = INVALID_TID;
        Push(family->members, tid, &Thread::nextMember);

        // Increase the allocation count
        family->dependencies.numThreadsAllocated++;
    }

    //
    // Update family information
    //
    if (thread->prevInBlock == INVALID_TID)
	{
		family->firstThreadInBlock = tid;
		DebugSimWrite("Set first thread in block for F%u to T%u (index %llu)", fid, tid, thread->index);
	}
	
	if (thread->isLastThreadInBlock)
	{
		family->lastThreadInBlock  = tid;
	}

    family->lastAllocated = tid;

    // Increase the index and counts
    if (thread->isLastThreadInFamily)
    {
        // We've allocated the last thread
        if (!DecreaseFamilyDependency(fid, *family, FAMDEP_ALLOCATION_DONE))
        {
            DeadlockWrite("Unable to mark ALLOCATION_DONE in family");
            return false;
        }
    }
    else if (++family->index % family->virtBlockSize == 0 && family->type == Family::GROUP && m_parent.GetPlaceSize() > 1)
    {
        // We've allocated the last in a block, skip to the next block
        Integer skip = (m_parent.GetPlaceSize() - 1) * family->virtBlockSize;
		if (!family->infinite && family->index >= family->nThreads - min(family->nThreads, skip))
        {
            // There are no more blocks for us
            if (!DecreaseFamilyDependency(fid, *family, FAMDEP_ALLOCATION_DONE))
            {
                DeadlockWrite("Unable to mark ALLOACTION_DONE in family");
                return false;
            }
        }
		family->index += skip;
    }
    
    ThreadQueue tq = {tid, tid};
    if (!ActivateThreads(tq))
    {
        // Abort allocation
        DeadlockWrite("Unable to activate thread");
        return false;
    }

    /*if (isFirstAllocatedThread && family->parent.lpid == m_lpid)
	{
		// We've created the first thread on the creating processor;
		// release the family's lock on the cache-line
		m_icache.ReleaseCacheLine(m_threadTable[tid].cid);
    }*/

    DebugSimWrite("Allocated thread for F%u at T%u", fid, tid);
	if (family->dependencies.allocationDone)
	{
	    DebugSimWrite("Set first thread in block for F%u to T%u (index %llu)", fid, tid, thread->index);
	}
    return true;
}

bool Allocator::WriteExitCode(RegIndex reg, ExitCode code)
{
    if (m_registerWrites.full())
    {
        return false;
        
    }
    COMMIT
    {
        RegisterWrite write;
        write.address = MAKE_REGADDR(RT_INTEGER, reg);
        write.value.m_state   = RST_FULL;
        write.value.m_integer = code;
        m_registerWrites.push(write);
    }
    return true;
}
                
// Called when a delegated family has synchronized remotely
// and sent the sync event to the parent (this) processor.
bool Allocator::OnRemoteSync(LFID fid, ExitCode /* code */)
{
    assert(m_familyTable[fid].place.type == PlaceID::DELEGATE);
    assert(m_familyTable[fid].parent.lpid == m_lpid);
    
    if (!DecreaseFamilyDependency(fid, FAMDEP_PREV_SYNCHRONIZED))
    {
        return false;
    }
    return true;
}

/// Called by the network to link the family on the parent core up with the
/// last created family in the group on the previous core
bool Allocator::SetupFamilyPrevLink(LFID fid, LFID link_prev)
{
    Family& family = m_familyTable[fid];
    
    assert(family.type      == Family::GROUP);
    assert(family.link_prev == INVALID_LFID);
    assert(!family.dependencies.nextTerminated);
    
    if (family.dependencies.numThreadsAllocated == 0 && family.dependencies.allocationDone)
    {
        // The family has been 'terminated', send the notification
        if (!m_network.SendFamilyTermination(link_prev))
        {
            return false;
        }
    }
    
    COMMIT
    {
        family.link_prev = link_prev;
        family.dependencies.nextTerminated = true;
    } 
    
    DebugSimWrite("Setting previous FID for F%u to F%u", fid, link_prev);
    return true;
}

bool Allocator::SetupFamilyNextLink(LFID fid, LFID link_next)
{
    Family& family = m_familyTable[fid];
    
    assert(family.type      == Family::GROUP);
    assert(family.link_next == INVALID_LFID);
    
    COMMIT{ family.link_next = link_next; } 
    DebugSimWrite("Setting next FID for F%u to F%u", fid, link_next);
    return true;
}

bool Allocator::SynchronizeFamily(LFID fid, Family& family, ExitCode code)
{
    // This is a group or delegated family or proxy
    if (family.parent.lpid == m_lpid)
    {
        // This is the parent CPU of the family. All other CPUs have
        // finished and synched. Write back the exit code
        if (family.exitCodeReg != INVALID_REG_INDEX)
        {
            WriteExitCode(family.exitCodeReg, code);
        }
        else if (family.parent.gpid != INVALID_GPID)
        {
            // This is a delegated family, send the exit code back as a sync event.
            if (!m_network.SendRemoteSync(family.parent.gpid, family.parent.fid, code))
            {
                return false;
            }
        }
    }
    // Send synchronization to next processor
    else if (!m_network.SendFamilySynchronization(family.link_next))
    {
        return false;
    }

    // Release registers
	RegIndex indices[NUM_REG_TYPES];
    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
		indices[i] = family.regs[i].base;
	}

	if (!m_raunit.Free(indices))
	{
		return false;
	}

    // Release member threads, if any
    if (family.members.head != INVALID_TID)
    {
        if (!m_threadTable.PushEmpty(family.members))
        {
            return false;
        }
    }
    
    if (m_exclusive == fid)
    {
        // The exclusively executing family has terminated, reset
        // the variable to allow a next family from the exclusive queue
        // to start.
        COMMIT{ m_exclusive = INVALID_LFID; }
    }

    COMMIT{ family.killed = true; }
    if (family.parent.gpid != INVALID_GPID) {
        // Killed a delegated family
        DebugSimWrite("Killed F%u (parent: F%u@P%u)", fid, family.parent.fid, family.parent.gpid);
    } else if (family.type == Family::GROUP) {
        // Killed a group family
        DebugSimWrite("Killed F%u (parent: T%u@P%u)", fid, family.parent.tid, family.parent.lpid);
    } else if (family.parent.tid != INVALID_TID) {
        // Killed a local family with a parent
        DebugSimWrite("Killed F%u (parent: T%u)", fid, family.parent.tid);
    } else {
        // Killed initial family (no parent)
        DebugSimWrite("Killed F%u", fid);
    }
    return true;
}

bool Allocator::DecreaseFamilyDependency(LFID fid, FamilyDependency dep)
{
    return DecreaseFamilyDependency(fid, m_familyTable[fid], dep);
}

bool Allocator::DecreaseFamilyDependency(LFID fid, Family& family, FamilyDependency dep)
{
    assert(family.state != FST_EMPTY);

    // We work on a copy unless we're committing
    Family::Dependencies  tmp_deps;
    Family::Dependencies* deps = &tmp_deps;
    if (IsCommitting()) {
        deps = &family.dependencies;
    } else {
        tmp_deps = family.dependencies;
    }

    switch (dep)
    {
    case FAMDEP_THREAD_COUNT:        assert(deps->numThreadsAllocated > 0); deps->numThreadsAllocated--;  break;
    case FAMDEP_OUTSTANDING_READS:   assert(deps->numPendingReads     > 0); deps->numPendingReads    --;  break;
    case FAMDEP_OUTSTANDING_SHAREDS: assert(deps->numPendingShareds   > 0); deps->numPendingShareds  --;  break;
    case FAMDEP_PREV_SYNCHRONIZED:   assert(!deps->prevSynchronized);       deps->prevSynchronized = true; break;
	case FAMDEP_ALLOCATION_DONE:     assert(!deps->allocationDone);         deps->allocationDone   = true; break;
    case FAMDEP_NEXT_TERMINATED:     assert(!deps->nextTerminated);         deps->nextTerminated   = true; break;
    }

    switch (dep)
    {
    case FAMDEP_THREAD_COUNT:
    case FAMDEP_ALLOCATION_DONE:
    case FAMDEP_NEXT_TERMINATED:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone && deps->nextTerminated)
        {
            // Forward the cleanup token when we have it, and all threads are done
            if (family.type != Family::LOCAL)
            {
    		    PSize placeSize = m_parent.GetPlaceSize();
    		    LPID  prev      = (m_lpid - 1 + placeSize) % placeSize;
                if (prev != family.parent.lpid)
                {
                    // The previous core is NOT the parent core, so send the termination notification
                    if (!m_network.SendFamilyTermination(family.link_prev))
                    {
                        DeadlockWrite("Unable to send family termination to F%u on previous processor", family.link_prev);
                        return false;
                    }
                }
            }
        }
        break;
        
    default:
        break;
    }

    switch (dep)
    {
    case FAMDEP_THREAD_COUNT:
    case FAMDEP_ALLOCATION_DONE:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone)
        {
            // It's considered 'killed' when all threads have gone
            COMMIT{
                family.state = FST_KILLED;

                if (family.members.head != INVALID_TID)
                {
                    // We executed threads, so notify CPU of family termination (for statistics).
                    // The Family PC identifies the thread.
                    m_parent.OnFamilyTerminatedLocally(family.pc);
                }
            }
        }
        // Fall through

    case FAMDEP_PREV_SYNCHRONIZED:
    case FAMDEP_OUTSTANDING_SHAREDS:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone &&
            deps->numPendingShareds   == 0 && deps->prevSynchronized)
        {
            // Family has terminated, synchronize it
            if (!SynchronizeFamily(fid, family, EXIT_NORMAL))
            {
                DeadlockWrite("Unable to kill family F%u", fid);
                return false;
            }
        }
        // Fall through

    case FAMDEP_NEXT_TERMINATED:
    case FAMDEP_OUTSTANDING_READS:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone &&
            deps->numPendingShareds   == 0 && deps->prevSynchronized &&
            deps->numPendingReads     == 0 && deps->nextTerminated)
        {
            COMMIT{ family.next = INVALID_LFID; }
			if (!m_familyTable.FreeFamily(fid))
            {
                DeadlockWrite("Unable to free family F%u", fid);
                return false;
            }
            DebugSimWrite("Cleaned up F%u", fid);
        }
        break;
    }

    return true;
}

bool Allocator::OnMemoryRead(LFID fid)
{
    COMMIT{ m_familyTable[fid].dependencies.numPendingReads++; }
    return true;
}

bool Allocator::DecreaseThreadDependency(LFID fid, TID tid, ThreadDependency dep)
{
    if (m_familyTable[fid].killed)
    {
        // Do nothing if the family has been killed
        return true;
    }
    
    // We work on a copy unless we're committing
    Thread::Dependencies tmp_deps;
    Thread::Dependencies* deps = &tmp_deps;
    Thread& thread = m_threadTable[tid];
    if (IsCommitting()) {
        deps = &thread.dependencies;
    } else {
        tmp_deps = thread.dependencies;
    }
    
    switch (dep)
    {
    case THREADDEP_OUTSTANDING_WRITES: deps->numPendingWrites--;   break;
    case THREADDEP_PREV_CLEANED_UP:    deps->prevCleanedUp = true; break;
    case THREADDEP_NEXT_TERMINATED:    deps->nextKilled    = true; break;
    case THREADDEP_TERMINATED:         deps->killed        = true; break;
    }
    
    switch (dep)
    {
    case THREADDEP_OUTSTANDING_WRITES:
        if (deps->numPendingWrites == 0 && thread.waitingForWrites)
        {
            // Wake up the thread that was waiting on it
            ThreadQueue tq = {tid, tid};
            if (!ActivateThreads(tq))
            {
                return false;
            }
            COMMIT{ thread.waitingForWrites = false; }
        }
        
    case THREADDEP_PREV_CLEANED_UP:
    case THREADDEP_NEXT_TERMINATED:
    case THREADDEP_TERMINATED:
        if (deps->numPendingWrites == 0 && deps->prevCleanedUp && deps->nextKilled && deps->killed)
        {
            // This thread can be cleaned up, push it on the cleanup queue
            if (m_cleanup.full())
            {
                return false;
            }
            COMMIT{ m_cleanup.push(tid); }
            break;
        }
    }
    
    return true;
}

bool Allocator::IncreaseThreadDependency(TID tid, ThreadDependency dep)
{
    COMMIT
    {
        Thread::Dependencies& deps = m_threadTable[tid].dependencies;
        switch (dep)
        {
        case THREADDEP_OUTSTANDING_WRITES: deps.numPendingWrites++; break;
        case THREADDEP_PREV_CLEANED_UP:    
        case THREADDEP_NEXT_TERMINATED:    
        case THREADDEP_TERMINATED:         assert(0); break;
        }
    }
    return true;
}

Family& Allocator::GetWritableFamilyEntry(LFID fid, TID parent) const
{
	Family& family = m_familyTable[fid];
	if (family.created || family.parent.lpid != m_lpid || family.parent.tid != parent)
	{
		// We only allow the parent thread (that allocated the entry) to fill it before creation
		throw InvalidArgumentException("Illegal family entry access");
	}
	return family;
}

// Initializes the family entry with default values.
// This is only called for families allocated locally with the ALLOCATE instruction.
void Allocator::SetDefaultFamilyEntry(LFID fid, TID parent, const RegisterBases bases[]) const
{
	COMMIT
	{
		Family& family = m_familyTable[fid];

		family.created       = false;
		family.legacy        = false;
		family.start         = 0;
		family.limit         = 1;
		family.step          = 1;
		family.virtBlockSize = 0;
		family.physBlockSize = 0;
		family.parent.gpid   = INVALID_GPID;
		family.parent.lpid   = m_lpid;
		family.parent.tid    = parent;
        family.link_prev     = INVALID_LFID;
        family.link_next     = INVALID_LFID;
                                                    		
		// Set default place
		family.place.type       = PlaceID::DEFAULT;
		family.place.exclusive  = false;
        family.place.pid        = 0;
		family.place.capability = 0;

		// By default, the globals and shareds are taken from the locals of the parent thread
		for (RegType i = 0; i < NUM_REG_TYPES; i++)
		{
			family.regs[i].globals = bases[i].globals;
			family.regs[i].shareds = bases[i].shareds;
		}
	}
}

// Allocates a family entry. Returns the LFID (and SUCCESS) if one is available,
// DELAYED if none is available and the request is written to the buffer. FAILED is
// returned if the buffer was full.
Result Allocator::AllocateFamily(TID parent, RegIndex reg, LFID* fid, const RegisterBases bases[])
{
	*fid = m_familyTable.AllocateFamily();
	if (*fid != INVALID_LFID)
	{
		// A family entry was free
		SetDefaultFamilyEntry(*fid, parent, bases);
		return SUCCESS;
	}

	if (!m_allocations.full())
	{
		// The buffer is not full, place the request in the buffer
		COMMIT
		{
			AllocRequest request;
			request.parent = parent;
			request.reg    = reg;
			std::copy(bases, bases + NUM_REG_TYPES, request.bases);
			m_allocations.push(request);
		}
		return DELAYED;
	}
	return FAILED;
}

bool Allocator::OnDelegatedCreate(const DelegateMessage& msg)
{
    assert(msg.parent.pid != INVALID_GPID);
    assert(msg.parent.fid != INVALID_LFID);
    
    Buffer<LFID>& queue = (msg.exclusive) ? m_createsEx : m_creates;
    if (queue.full())
    {
        return false;
    }

	LFID fid = m_familyTable.AllocateFamily();
	if (fid != INVALID_LFID)
	{
	    // Copy the data
        COMMIT
        {
            Family& family = m_familyTable[fid];
            family.created       = false;
            family.legacy        = false;
            family.start         = msg.start;
            family.limit         = msg.limit;
            family.step          = msg.step;
            family.virtBlockSize = msg.blockSize;
            family.physBlockSize = 0;
            family.parent.gpid   = msg.parent.pid;
            family.parent.lpid   = m_lpid;
            family.parent.fid    = msg.parent.fid;
            family.link_prev     = INVALID_LFID;
            family.link_next     = INVALID_LFID;
            family.pc            = msg.address;
            family.exitCodeReg   = INVALID_REG_INDEX;
            family.state         = FST_CREATE_QUEUED;

    		// Set place to group
    		family.place.type       = PlaceID::GROUP;
    		family.place.exclusive  = msg.exclusive;
            family.place.pid        = 0;
    		family.place.capability = 0;

            // Lock the family
            family.created = true;

    		for (RegType i = 0; i < NUM_REG_TYPES; i++)
    		{
    			family.regs[i].globals = INVALID_REG_INDEX;
    			family.regs[i].shareds = INVALID_REG_INDEX;
    		}

            // Push the create
            queue.push(fid);
        }
        DebugSimWrite("Queued delegated create by F%u@P%u at 0x%llx", msg.parent.fid, msg.parent.pid, (unsigned long long)msg.address);
        return true;
	}
	return false;
}

LFID Allocator::OnGroupCreate(const CreateMessage& msg, LFID link_next)
{
	LFID fid = m_familyTable.AllocateFamily();
	if (fid == INVALID_LFID)
	{
        // Couldn't allocate an entry
        return INVALID_LFID;
    }

    DebugSimWrite("Allocated family F%u for group create", fid);

    // Copy the data
	Family& family = m_familyTable[fid];
	family.type          = Family::GROUP;
	family.legacy        = false;
	family.infinite      = msg.infinite;
	family.start         = msg.start;
	family.step          = msg.step;
	family.nThreads      = msg.nThreads;
	family.parent.gpid   = msg.parent.gpid;
	family.parent.lpid   = msg.parent.lpid;
	family.parent.tid    = msg.parent.tid;
	family.virtBlockSize = msg.virtBlockSize;
    family.physBlockSize = msg.physBlockSize;
	family.pc            = msg.address;
	family.hasDependency = false;
    family.link_prev     = msg.link_prev;
    family.link_next     = link_next;
	for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
        family.regs[i].globals = INVALID_REG_INDEX;
        family.regs[i].shareds = INVALID_REG_INDEX;
        family.regs[i].count   = msg.regsNo[i];
	}

	// Initialize the family
	InitializeFamily(fid, Family::GROUP);

	// Allocate the registers
	if (!AllocateRegisters(fid))
	{
		return INVALID_LFID;
	}

    if (!ActivateFamily(fid))
    {
        return INVALID_LFID;
	}
	return fid;
}

bool Allocator::ActivateFamily(LFID fid)
{
	Push(m_alloc, fid);
	return true;
}

void Allocator::InitializeFamily(LFID fid, Family::Type type) const
{
	COMMIT
	{
		Family& family    = m_familyTable[fid];
		PSize   placeSize = m_parent.GetPlaceSize();
		
		family.members.head = INVALID_TID;
		family.killed       = false;
		family.state        = (type == Family::DELEGATED) ? FST_DELEGATED : FST_IDLE;
		family.next         = INVALID_LFID;
		family.type         = type;
		
	    // Dependencies
	    family.dependencies.allocationDone      = true;
	    family.dependencies.numPendingReads     = 0;
	    family.dependencies.numThreadsAllocated = 0;
	    family.dependencies.prevSynchronized    = false;
	    family.dependencies.nextTerminated      = true;
        family.dependencies.numPendingShareds   = 0;
		
	    // Dependency information
	    family.hasDependency      = false;
	    family.lastThreadInBlock  = INVALID_TID;
	    family.firstThreadInBlock = INVALID_TID;
	    family.lastAllocated      = INVALID_TID;

		// Register bases
	    for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            Family::RegInfo& regs = family.regs[i];

            // Adjust register bases if necessary
            if (regs.globals != INVALID_REG_INDEX)
            {
                if (regs.count.globals == 0) {
                    // We have no parent globals
                    regs.globals = INVALID_REG_INDEX;
                } else if (regs.shareds < regs.globals) {
                    // In case globals and shareds overlap, move globals up
                    regs.globals = max(regs.shareds + regs.count.shareds, regs.globals);
                }
            }

            if (regs.shareds != INVALID_REG_INDEX)
            {
                if (regs.count.shareds == 0) {
                    // We have no parent shareds
                    regs.shareds = INVALID_REG_INDEX;
                } else if (regs.globals <= regs.shareds) {
                    // In case globals and shareds overlap, move shareds up
                    regs.shareds = max(regs.globals + regs.count.globals, regs.shareds);
                }
            }
    		
	    	if (family.parent.gpid == INVALID_GPID)
    		{
    		    // If we're a delegated create, we don't care about the no. of
    		    // pending shareds. They are managed by the proxy.
    	    	family.dependencies.numPendingShareds += family.regs[i].count.shareds;
    	    }
    	    
		    if (regs.count.shareds > 0)
		    {
       	    	family.hasDependency = true;
            }
        }
        
		if (type != Family::DELEGATED)
		{
	    	// This core will execute the first thread in the family
    		LPID first_pid = (family.parent.lpid + 1) % placeSize;

    	    family.dependencies.allocationDone   = false;
	        family.dependencies.prevSynchronized = (type == Family::LOCAL) || (m_lpid == first_pid); 
	        family.dependencies.nextTerminated   = (type == Family::LOCAL);
	    
		    family.index = (type == Family::LOCAL) ? 0 : ((placeSize + m_lpid - first_pid) % placeSize) * family.virtBlockSize;
		    
            // Calculate which CPU will run the last thread
            LPID last_pid = (first_pid + (LPID)((max<uint64_t>(1, family.nThreads) - 1) / family.virtBlockSize)) % placeSize;
                
            if (type == Family::LOCAL || last_pid == family.parent.lpid || m_lpid != family.parent.lpid)
            {
                // Ignore the pending shareds count
                family.dependencies.numPendingShareds = 0;
            }
	    }
    }
}

bool Allocator::AllocateRegisters(LFID fid)
{
    // Try to allocate registers
	Family& family = m_familyTable[fid];

    if (family.physBlockSize == 0) 
    { 
        // We have nothing to allocate, so it succeeded 
        for (RegType i = 0; i < NUM_REG_TYPES; i++) 
        { 
            Family::RegInfo& regs = family.regs[i]; 
            COMMIT 
            { 
                regs.base   = INVALID_REG_INDEX; 
                regs.size   = 0; 
                regs.latest = INVALID_REG_INDEX; 
            } 
        } 
        return true; 
    } 	

	for (FSize physBlockSize = min(family.physBlockSize, m_threadTable.GetNumThreads()); physBlockSize > 0; physBlockSize--)
    {
		// Calculate register requirements
		RegSize sizes[NUM_REG_TYPES];
        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            const Family::RegInfo& regs = family.regs[i];
            
            // The family needs a cache for remote shareds if it's a group create,
            // or if it's a delegated create (to receive from parent)
            const bool need_shared_cache = (family.type == Family::GROUP || family.parent.gpid != INVALID_GPID);

            sizes[i] = (regs.count.locals + regs.count.shareds) * physBlockSize;
			if (regs.globals == INVALID_REG_INDEX) sizes[i] += regs.count.globals; // Add the cache for the globals
			if (need_shared_cache)   		       sizes[i] += regs.count.shareds; // Add the cache for the remote shareds
		}

		RegIndex indices[NUM_REG_TYPES];
		if (m_raunit.Alloc(sizes, fid, indices))
		{
			// Success, we have registers for all types
			COMMIT{ family.physBlockSize = physBlockSize; }
			
			for (RegType i = 0; i < NUM_REG_TYPES; i++)
			{
				Family::RegInfo& regs = family.regs[i];
				COMMIT
				{
					regs.base            = INVALID_REG_INDEX;
					regs.size            = sizes[i];
					regs.latest          = INVALID_REG_INDEX;
				}

				if (sizes[i] > 0)
				{
					// Clear the allocated registers
					m_registerFile.Clear(MAKE_REGADDR(i, indices[i]), sizes[i]);

					COMMIT
					{
						regs.base = indices[i];

						// Point globals and shareds to their local cache
						if (regs.globals == INVALID_REG_INDEX && regs.count.globals > 0) regs.globals = regs.base + regs.size - regs.count.shareds - regs.count.globals;
						if (regs.shareds == INVALID_REG_INDEX && regs.count.shareds > 0) regs.shareds = regs.base + regs.size - regs.count.shareds;
					}
				}
				DebugSimWrite("%d: Allocated %u registers at 0x%04x", i, sizes[i], indices[i]);
			}
			return true;
        }
    }

    return false;
}

Result Allocator::OnCycleReadPhase(unsigned int stateIndex)
{
    if (stateIndex == 0)
    {
        if (m_allocating == INVALID_LFID && m_alloc.head != INVALID_LFID)
        {
            // Get next family to allocate
    	    COMMIT{ m_allocating = Pop(m_alloc); }
		    DebugSimWrite("Starting thread allocation from F%u", m_allocating );
		    return SUCCESS;
        }
    }
    return DELAYED;
}

void Allocator::UpdateStatistics()
{
    m_totalActiveQueueSize += m_activeQueueSize;
    m_maxActiveQueueSize = max(m_maxActiveQueueSize, m_activeQueueSize);
    m_minActiveQueueSize = min(m_minActiveQueueSize, m_activeQueueSize);
}

bool Allocator::OnCachelineLoaded(CID cid)
{
	assert(m_createFID != INVALID_LFID);
	COMMIT{
		m_createState = CREATE_LINE_LOADED;
		m_createLine  = cid;
	}
	return true;
}

Result Allocator::OnCycleWritePhase(unsigned int stateIndex)
{
    switch (stateIndex)
    {
    case 0:
        //
        // Cleanup (reallocation) takes precedence over initial allocation
        //
        if (!m_cleanup.empty())
        {
            TID     tid    = m_cleanup.front();
            Thread& thread = m_threadTable[tid];
            LFID    fid    = thread.family;
            Family& family = m_familyTable[fid];

            assert(thread.state == TST_KILLED);

			if (family.hasDependency && !thread.isLastThreadInFamily && (family.type == Family::GROUP || family.physBlockSize > 1))
            {
                // Mark 'previous thread cleaned up' on the next thread
                if (!thread.isLastThreadInBlock)
                {
                    assert(thread.nextInBlock != INVALID_TID);
                    if (!DecreaseThreadDependency(fid, thread.nextInBlock, THREADDEP_PREV_CLEANED_UP))
                    {
                        DeadlockWrite("Unable to mark PREV_CLEANED_UP for T%u's next thread T%u in F%u", tid, thread.nextInBlock, fid);
                        return FAILED;
                    }
                }
                else if (!m_network.SendThreadCleanup(family.link_next))
                {
                    DeadlockWrite("Unable to send thread cleanup notification for T%u in F%u", tid, fid);
                    return FAILED;
                }
            }
            
            COMMIT
            {
                // Unreserve the TLS memory
                const MemAddr tls_base = CalculateTLSAddress(fid, tid);
                m_parent.UnreserveTLS(tls_base);
            }

            if (family.dependencies.allocationDone)
            {
                // With cleanup we don't do anything to the thread. We just forget about it.
                // It will be recycled once the family terminates.
                COMMIT{ thread.state = TST_UNUSED; }

                // Cleanup
                if (!DecreaseFamilyDependency(fid, FAMDEP_THREAD_COUNT))
                {
                    DeadlockWrite("Unable to decrease thread count during cleanup of T%u in F%u", tid, fid);
                    return FAILED;
                }

                DebugSimWrite("Cleaned up T%u for F%u (index %llu)", tid, fid, thread.index);
			}
            else
            {
                // Reallocate thread
                if (!AllocateThread(fid, tid, false))
                {
                    DeadlockWrite("Unable to reallocate thread T%u in F%u", tid, fid);
                    return FAILED;
                }

                if (family.dependencies.allocationDone && m_allocating == fid)
                {
                    // Go to next family
                    DebugSimWrite("Done allocating from F%u", m_allocating);
                    COMMIT{ m_allocating = INVALID_LFID; }
                }
            }
            COMMIT{ m_cleanup.pop(); }
			return SUCCESS;
        }
        
		if (m_allocating != INVALID_LFID)
        {
            // Allocate an initial thread of a family
            Family& family = m_familyTable[m_allocating];

            bool done = true;
            if (family.infinite || (family.nThreads > 0 && family.index < family.nThreads))
            {
                // We have threads to run
                TID tid = m_threadTable.PopEmpty();
                if (tid == INVALID_TID)
                {
                    DeadlockWrite("Unable to allocate a free thread entry for F%u", m_allocating);
                    return FAILED;
                }
            
                if (!AllocateThread(m_allocating, tid))
                {
                    DeadlockWrite("Unable to allocate new thread T%u for F%u", tid, m_allocating);
                    return FAILED;
                }

                // Check if we're done with the initial allocation of this family
                done = (family.dependencies.numThreadsAllocated == family.physBlockSize || family.dependencies.allocationDone);
            }
            // We don't have any threads to run
            else if (!DecreaseFamilyDependency(m_allocating, FAMDEP_ALLOCATION_DONE))
            {
                return FAILED;
            }
            
            if (done)
            {
                // We're done with this family
                DebugSimWrite("Done allocating from F%u", m_allocating);
                COMMIT{ m_allocating = INVALID_LFID; }
            }
			return SUCCESS;
        }
        break;

	case 1:
        if (!m_allocations.empty())
		{
			const AllocRequest& req = m_allocations.front();
			LFID fid = m_familyTable.AllocateFamily();
			if (fid == INVALID_LFID)
			{
			    DeadlockWrite("Unable to allocate a free family entry (target R%04x)", req.reg);
				return FAILED;
			}
			
			// A family entry was free
			SetDefaultFamilyEntry(fid, req.parent, req.bases);

            // Writeback the FID
            RegisterWrite write;
			write.address = MAKE_REGADDR(RT_INTEGER, req.reg);
			write.value.m_state   = RST_FULL;
			write.value.m_integer = fid;
            if (!m_registerWrites.push(write))
            {
                DeadlockWrite("Unable to queue write to register R%04x", req.reg);
                return FAILED;
            }
			COMMIT{m_allocations.pop();}
			return SUCCESS;
		}
		break;

	case 2:
        if (m_createState == CREATE_INITIAL)
        {
            // Pick a create queue to create from
            LFID fid = INVALID_LFID;
            const char* ex_type = NULL;
            if (m_exclusive == INVALID_LFID && !m_createsEx.empty())
            {
                fid = m_createsEx.front();
                ex_type = "exclusive";
                COMMIT{
                    m_createsEx.pop();
                    m_exclusive = fid;
                }
            }
            else if (!m_creates.empty())
            {
                fid = m_creates.front();
                ex_type = "non-exclusive";
                COMMIT{ m_creates.pop(); }
            }
            
            if (fid != INVALID_LFID)
            {
    			Family& family = m_familyTable[fid];
    			
                // Determine create type
                const char* create_type = "???";
                if (family.place.type == PlaceID::LOCAL) {
                    create_type = "local";
                } else if (family.place.type == PlaceID::DELEGATE) {
                    create_type = "delegated";
                } else {
                    create_type = "default";
                }
                
                // Determine exclusiveness
                DebugSimWrite("Processing %s %s create for F%u", ex_type, create_type, fid);

				// Load the register counts from the family's first cache line
				Instruction counts;
				CID         cid;
				Result      result;
				if ((result = m_icache.Fetch(family.pc - sizeof(Instruction), sizeof(counts), cid)) == FAILED)
				{
				    DeadlockWrite("Unable to fetch the I-Cache line for 0x%016llx for F%u", (unsigned long long)family.pc, fid);
					return FAILED;
				}

				if (result == SUCCESS)
				{
					// Cache hit, proceed to loaded stage
                	COMMIT{
		                m_createState = CREATE_LINE_LOADED;
		                m_createLine  = cid;
	                }
				}
				else
				{
					// Cache miss, line is being fetched.
					// The I-Cache will notify us with onCachelineLoaded().
					COMMIT{
					    m_createState = CREATE_LOADING_LINE;
					}
				}
				
				COMMIT
				{
				    m_createFID  = fid;
				    family.state = FST_CREATING;
			    }
				return SUCCESS;
			}
	    }
		else if (m_createState == CREATE_LINE_LOADED)
		{
		    assert(m_createFID != INVALID_LFID);
   			Family& family = m_familyTable[m_createFID];
		    
			// Read the cache-line
			Instruction counts;
			if (!m_icache.Read(m_createLine, family.pc - sizeof(Instruction), &counts, sizeof(counts)))
			{
			    DeadlockWrite("Unable to read the I-Cache line for 0x%016llx for F%u", (unsigned long long)family.pc, m_createFID);
				return FAILED;
			}
    	    counts = UnserializeInstruction(&counts);

            RegsNo regcounts[NUM_REG_TYPES];
            bool   hasDependency = false;
            for (RegType i = 0; i < NUM_REG_TYPES; i++)
            {
                Instruction c = counts >> (i * 16);
                regcounts[i].globals = (unsigned char)((c >>  0) & 0x1F);
                regcounts[i].shareds = (unsigned char)((c >>  5) & 0x1F);
                regcounts[i].locals  = (unsigned char)((c >> 10) & 0x1F);
                if (regcounts[i].shareds > 0)
                {
                    hasDependency = true;
                }
            }
            
            // Release the cache-lined held by the create so far
            if (!m_icache.ReleaseCacheLine(m_createLine))
            {
    		    DeadlockWrite("Unable to release cache line for F%u", m_createFID);
                return FAILED;
            }
                
            COMMIT
            {
                for (RegType i = 0; i < NUM_REG_TYPES; i++)
                {
                    family.regs[i].base  = INVALID_REG_INDEX;
                    family.regs[i].count = regcounts[i];
                }
            }
            
            Family::Type type;
            if (family.place.type == PlaceID::DELEGATE)
            {
                type = Family::DELEGATED;
                
                // Delegated create; send the create -- no further processing required on this core
                if (!m_network.SendDelegatedCreate(m_createFID))
                {
 				    DeadlockWrite("Unable to send the delegation for F%u", m_createFID);
                    return FAILED;
                }

                // Reset the create state; we're done with this create
    	        COMMIT{ m_createState = CREATE_INITIAL; }
            }
            else
            {
                // For local and group creates, sanitize the family
                bool local = SanitizeFamily(family, hasDependency);
    		    if (!local)
    			{
    				// Group create, request the create token
    			    if (!m_network.RequestToken())
   					{
    				    DeadlockWrite("Unable to request the create token from the network for F%u", m_createFID);
    					return FAILED;
    				}
    				type = Family::GROUP;
    			    COMMIT{ m_createState = CREATE_ACQUIRING_TOKEN; }
    			}
    			else
    			{
    				// Local family, skip straight to allocating registers
    				type = Family::LOCAL;
    				COMMIT{ m_createState = CREATE_ALLOCATING_REGISTERS; }
    			}
            }
        	InitializeFamily(m_createFID, type);
    	    return SUCCESS;
        }
		else if (m_createState == CREATE_BROADCASTING_CREATE)
		{
		    assert(m_createFID != INVALID_LFID);
		    
			// We have the token; broadcast the create
			if (!m_network.SendGroupCreate(m_createFID))
			{
			    DeadlockWrite("Unable to send the create for F%u", m_createFID);
				return FAILED;
			}

			// Advance to next stage
			COMMIT{ m_createState = CREATE_ALLOCATING_REGISTERS; }
    	    return SUCCESS;
		}
		else if (m_createState == CREATE_ALLOCATING_REGISTERS)
		{
		    assert(m_createFID != INVALID_LFID);
		    
			// Allocate the registers
			if (!AllocateRegisters(m_createFID))
			{
			    DeadlockWrite("Unable to allocate registers for F%u", m_createFID);
				return FAILED;
			}

			// Family's now created, we can start creating threads
			if (!ActivateFamily(m_createFID))
			{
			    DeadlockWrite("Unable to activate the family F%u", m_createFID);
				return FAILED;
			}

            // Reset the create state
		    COMMIT
		    {
		        m_createState = CREATE_INITIAL;
		        m_createFID   = INVALID_LFID;
		    }
    	    return SUCCESS;
        }
        break;

    case 3:
        if (m_readyThreads.head != INVALID_TID)
        {
            TID     tid    = m_readyThreads.head;
            Thread& thread = m_threadTable[tid];

            COMMIT
            {
                // Pop the thread
                m_readyThreads.head = thread.nextState;
            }
            
            // This thread doesn't have a Thread Instruction Buffer yet,
            // so try to get the cache line
            TID    next = tid;
	        CID    cid;
	        Result result = SUCCESS;
            if ((result = m_icache.Fetch(thread.pc, sizeof(Instruction), next, cid)) == FAILED)
            {
                // We cannot fetch, abort activation
                DeadlockWrite("Unable to fetch the I-Cache line for 0x%016llx to activate thread T%u", (unsigned long long)thread.pc, tid);
                return FAILED;
            }
    
            if (result != SUCCESS)
            {
                // Request was delayed, link thread into waiting queue
                // Mark the thread as waiting
                COMMIT
                {
                    thread.cid       = cid;
                    thread.nextState = next;
                    thread.state     = TST_WAITING;
                }
            }
	        else
	        {
                COMMIT
                {
                    thread.cid       = cid;
                    thread.nextState = INVALID_TID;
                }

                // The thread can be added to the family's active queue
                ThreadQueue tq = {tid, tid};
                if (!QueueActiveThreads(tq))
                {
                    DeadlockWrite("Unable to enqueue T%u to the Active Queue", tid);
                    return FAILED;
                }
            }
            return SUCCESS;
        }
        break;

    case 4:
		if (!m_registerWrites.empty())
        {
            RegAddr addr = m_registerWrites.front().address;
            if (!m_registerFile.p_asyncW.Write(addr))
            {
                DeadlockWrite("Unable to acquire the port for the queued register write to %s", addr.str().c_str());
                return FAILED;
            }

            if (!m_registerFile.WriteRegister(m_registerWrites.front().address, m_registerWrites.front().value, false))
            {
                DeadlockWrite("Unable to write the queued register write to %s", addr.str().c_str());
                return FAILED;
            }

            COMMIT{ m_registerWrites.pop(); }
			return SUCCESS;
        }
        break;
    }

    return DELAYED;
}

bool Allocator::OnTokenReceived()
{
    // The network told us we can create this family (group family, local create)
    assert(m_createFID != INVALID_LFID);
	assert(m_createState == CREATE_ACQUIRING_TOKEN);
	COMMIT{ m_createState = CREATE_BROADCASTING_CREATE; }
    return true;
}

// Sanitizes the limit, block size and local/group flag.
// Use only for non-delegated creates.
// Returns whether the sanitized family is local (not means group).
bool Allocator::SanitizeFamily(Family& family, bool hasDependency)
{
    bool local = (family.place.type == PlaceID::LOCAL);
    if (m_parent.GetPlaceSize() == 1)
    {
        // Force to local
        local = true;
    }

    // Sanitize the family entry
    Integer nBlock;
    Integer nThreads = 0;
    Integer step;
    if (family.step != 0)
    {
        // Finite family

        Integer diff = 0;
        if (family.step > 0)
        {
            if (family.limit > family.start) {
                diff = family.limit - family.start;
            }
            step = family.step;
        } else {
            if (family.limit < family.start) {
                diff = family.start - family.limit;
            }
            step = -family.step;
        }

        nThreads = (diff + step - 1) / step;

        if (family.virtBlockSize == 0 || !hasDependency)
        {
            // Balance threads as best as possible
            PSize placeSize = m_parent.GetPlaceSize();
            nBlock = (!local)
                ? (nThreads + placeSize - 1) / placeSize  // Group family, divide threads evenly (round up)
                : nThreads;                               // Local family, take as many as possible
        }
        else
        {
            nBlock = family.virtBlockSize;
        }

        if (nThreads <= nBlock)
        {
            // #threads <= blocksize: force to local
            local  = true;
            nBlock = nThreads;
        }

        step = family.step;
    }
    else
    {
        // Infinite family
        nBlock = (family.virtBlockSize > 0)
            ? family.virtBlockSize
            : m_threadTable.GetNumThreads();

        // Use the limit as step
        step = family.limit;
    }

    COMMIT
    {
        family.infinite = (family.step == 0);
        family.step     = step;

        // For independent families, use the original virtual block size
        // as physical block size.
        family.physBlockSize = (TSize)((family.virtBlockSize == 0 || hasDependency)
                             ? nBlock
                             : family.virtBlockSize);
        family.virtBlockSize = max<Integer>(nBlock,1);
        family.nThreads      = nThreads;
    }
    return local;
}

// Locally issued creates
bool Allocator::QueueCreate(LFID fid, MemAddr address, TID parent, RegIndex exitCodeReg)
{
	assert(parent != INVALID_TID);

    Family& family = GetWritableFamilyEntry(fid, parent);

    Buffer<LFID>& queue = family.place.exclusive ? m_createsEx : m_creates;
    if (queue.full())
    {
		return false;
	}

	COMMIT
    {
		// Store the information
		family.pc           = (address & -(MemAddr)m_icache.GetLineSize()) + 2 * sizeof(Instruction); // Skip control and reg count words
		family.exitCodeReg  = exitCodeReg;
	    family.state        = FST_CREATE_QUEUED;

		// Lock the family
		family.created = true;

		// Push the create
		queue.push(fid);
    }
    DebugSimWrite("Queued local create by T%u at 0x%llx", parent, (unsigned long long)address);
    return true;
}

Allocator::Allocator(Processor& parent, const string& name,
    FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
    LPID lpid, const Config& config)
:
    IComponent(&parent, parent.GetKernel(), name, "thread-allocate|family-allocate|family-create|thread-activation|reg-write-queue"),
    m_parent(parent), m_familyTable(familyTable), m_threadTable(threadTable), m_registerFile(registerFile), m_raunit(raunit), m_icache(icache), m_network(network), m_pipeline(pipeline),
    m_lpid(lpid), m_activeQueueSize(0), m_totalActiveQueueSize(0), m_maxActiveQueueSize(0), m_minActiveQueueSize(UINT64_MAX),
    m_creates       (config.getInteger<BufferSize>("LocalCreatesQueueSize", INFINITE)),
    m_createsEx     (config.getInteger<BufferSize>("LocalExclusiveCreatesQueueSize", INFINITE)),
    m_registerWrites(config.getInteger<BufferSize>("RegisterWritesQueueSize", INFINITE)),
    m_cleanup       (config.getInteger<BufferSize>("ThreadCleanupQueueSize", INFINITE)),
	m_allocations   (config.getInteger<BufferSize>("FamilyAllocationQueueSize", INFINITE)),
	m_createFID(INVALID_LFID), m_createState(CREATE_INITIAL)
{
    m_allocating   = INVALID_LFID;
    m_alloc.head   = INVALID_LFID;
    m_alloc.tail   = INVALID_LFID;
    m_exclusive    = INVALID_LFID;
    m_activeThreads.head = INVALID_TID;
    m_activeThreads.tail = INVALID_TID;
    m_readyThreads.head  = INVALID_TID;
    m_readyThreads.tail  = INVALID_TID;
}

void Allocator::AllocateInitialFamily(MemAddr pc)
{
    static const unsigned char InitialRegisters[NUM_REG_TYPES] = {31, 31};

	LFID fid = m_familyTable.AllocateFamily();
	if (fid == INVALID_LFID)
	{
		throw SimulationException("Unable to create initial family");
	}

	Family& family = m_familyTable[fid];
	family.start         = 0;
    family.step          = 1;
	family.nThreads      = 1;
	family.virtBlockSize = 1;
	family.physBlockSize = 1;
	family.parent.tid    = INVALID_TID;
	family.parent.lpid   = m_lpid;
	family.parent.gpid   = INVALID_GPID;
	family.exitCodeReg   = INVALID_REG_INDEX;
	family.created       = true;
	family.legacy        = false;
	family.pc            = pc;
    family.link_prev     = INVALID_LFID;
    family.link_next     = INVALID_LFID;

	// Set initial place to the whole group
	family.place.type       = PlaceID::GROUP;
	family.place.pid        = 0;
	family.place.capability = 0;
	family.place.exclusive  = false;
	
	for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
		family.regs[i].count.locals  = InitialRegisters[i];
        family.regs[i].count.globals = 0;
        family.regs[i].count.shareds = 0;
        family.regs[i].globals = INVALID_REG_INDEX;
        family.regs[i].shareds = INVALID_REG_INDEX;
    };

	InitializeFamily(fid, Family::LOCAL);

    if (//!m_icache.Fetch(family.pc, sizeof(Instruction)) ||
	    !AllocateRegisters(fid) ||
	    !ActivateFamily(fid)
	   )
	{
		throw SimulationException("Unable to create initial family");
	}
}

void Allocator::Push(FamilyQueue& q, LFID fid)
{
    COMMIT
    {
        if (q.head == INVALID_LFID) {
            q.head = fid;
        } else {
            m_familyTable[q.tail].next = fid;
        }
        q.tail = fid;
    }
}

void Allocator::Push(ThreadQueue& q, TID tid, TID Thread::*link)
{
    COMMIT
    {
        if (q.head == INVALID_TID) {
            q.head = tid;
        } else {
            m_threadTable[q.tail].*link = tid;
        }
        q.tail = tid;
    }
}

LFID Allocator::Pop(FamilyQueue& q)
{
    LFID fid = q.head;
    if (q.head != INVALID_LFID)
    {
        q.head = m_familyTable[fid].next;
    }
    return fid;
}

TID Allocator::Pop(ThreadQueue& q, TID Thread::*link)
{
    TID tid = q.head;
    if (q.head != INVALID_TID)
    {
        q.head = m_threadTable[tid].*link;
    }
    return tid;
}
