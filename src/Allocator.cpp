#include "Allocator.h"
#include "Processor.h"
#include "Pipeline.h"
#include "Network.h"
#include "config.h"
#include "symtable.h"
#include <cassert>
#include <cmath>
#include <iomanip>
using namespace std;

namespace Simulator
{

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
            base = (regs.parent_globals == INVALID_REG_INDEX)
                ? regs.base + regs.size - regs.count.shareds - regs.count.globals
                : regs.parent_globals;
            break;
            
        case RRT_PARENT_SHARED:
            // This should only be used if the actual parent thread is on this CPU
            assert(family.type == Family::GROUP || family.type == Family::DELEGATED);
            assert(family.parent.gpid == INVALID_GPID);
            assert(family.parent.lpid == m_lpid);
            assert(regs.parent_shareds != INVALID_REG_INDEX);

            // Return the shared address in the parent thread
            base = regs.parent_shareds;
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

    if (regs.parent_globals == INVALID_REG_INDEX)
    {
        const RegIndex globals = regs.base + regs.size - regs.count.shareds - regs.count.globals;
        if (addr.index >= globals && addr.index < globals + regs.count.globals)
        {
            // It's a global
            *group = RC_GLOBAL;
            return INVALID_TID;
        }
    }
    
    if (regs.parent_shareds == INVALID_REG_INDEX)
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
    if (!p_activeThreads.Invoke())
    {
        DeadlockWrite("Unable to acquire arbitrator for Active Queue");
        return false;
    }
    
    if (!QueueThreads(m_activeThreads, threads, TST_ACTIVE))
    {
        DeadlockWrite("Unable to queue threads onto Active Queue");
        return false;
    }
    
    return true;
}

TID Allocator::PopActiveThread()
{
    TID tid = INVALID_TID;
    if (!m_activeThreads.Empty())
    {   
        tid = m_activeThreads.Front();
        m_activeThreads.Pop();
    }
    return tid;
}

//
// Adds the list of threads to the family's active queue.
// There is assumed to be a linked list between the threads.head
// and threads.tail thread by Thread::nextState.
//
bool Allocator::QueueThreads(ThreadList& list, const ThreadQueue& threads, ThreadState state)
{
    assert(threads.head != INVALID_TID);
    assert(threads.tail != INVALID_TID);

    // Append the threads to the list
    list.Append(threads.head, threads.tail);

    COMMIT
    {
        // Admin: Mark the threads
        TID    next  = threads.head, cur;
        size_t count = 0;
        do
        {
            cur = next;
            next = m_threadTable[cur].nextState;
            m_threadTable[cur].state = state;
            ++count;
        } while (cur != threads.tail);
    }
    return true;
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
    if (!DecreaseThreadDependency(family.lastThreadInBlock, THREADDEP_NEXT_TERMINATED))
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

    if (!DecreaseThreadDependency(family.firstThreadInBlock, THREADDEP_PREV_CLEANED_UP))
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
    if (!p_readyThreads.Invoke())
    {
        DeadlockWrite("Unable to acquire arbitrator for Ready Queue");
        return false;
    }
    
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
            if (!DecreaseThreadDependency(thread.prevInBlock, THREADDEP_NEXT_TERMINATED))
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
        OutputWrite("Thread T%u (F%u) did not write all its shareds", (unsigned)tid, (unsigned)thread.family);
    }

    // Mark the thread as killed
    if (!DecreaseThreadDependency(tid, THREADDEP_TERMINATED))
    {
        return false;
    }

    DebugSimWrite("Killed thread T%u", (unsigned)tid);

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
        DeadlockWrite("Unable to release I-Cache line #%u", (unsigned)thread.cid);
        return false;
    }

    // The thread can be added to the ready queue
    ThreadQueue tq = {tid, tid};
    if (!ActivateThreads(tq))
    {
        DeadlockWrite("Unable to reschedule T%u in F%u", (unsigned)tid, (unsigned)thread.family);
        return false;
    }

    DebugSimWrite("Rescheduling thread T%u in F%u to %s", (unsigned)tid, (unsigned)thread.family, 
                  GetKernel()->GetSymbolTable()[pc].c_str());
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
            ? predecessor->regs[i].base         // Producer runs on the same processor (predecessor)
            : (family->type == Family::LOCAL
               ? family->regs[i].parent_shareds // Producer runs on the same processor (parent)
               : INVALID_REG_INDEX);            // Producer runs on the previous processor (parent or predecessor)

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

    COMMIT
    {
        // Reserve the memory (commits on use)
        const MemAddr tls_base = CalculateTLSAddress(fid, tid);
        const MemSize tls_size = CalculateTLSSize();
        m_parent.ReserveTLS(tls_base, tls_size);
    }

    // Write L0 to the register file
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
        DebugSimWrite("Set first thread in block for F%u to T%u (index %llu)", (unsigned)fid, (unsigned)tid, (unsigned long long)thread->index);
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

    DebugSimWrite("Allocated thread for F%u at T%u", (unsigned)fid, (unsigned)tid);
    return true;
}

bool Allocator::WriteExitCode(RegIndex reg, ExitCode code)
{
    RegisterWrite write;
    write.address = MAKE_REGADDR(RT_INTEGER, reg);
    write.value.m_state   = RST_FULL;
    write.value.m_integer = code;
    if (!m_registerWrites.Push(write))
    {
        return false;
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
    
    DebugSimWrite("Setting previous FID for F%u to F%u", (unsigned)fid, (unsigned)link_prev);
    return true;
}

bool Allocator::SetupFamilyNextLink(LFID fid, LFID link_next)
{
    Family& family = m_familyTable[fid];
    
    assert(family.type      == Family::GROUP);
    assert(family.link_next == INVALID_LFID);
    
    COMMIT{ family.link_next = link_next; } 
    DebugSimWrite("Setting next FID for F%u to F%u", (unsigned)fid, (unsigned)link_next);
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
            if (!WriteExitCode(family.exitCodeReg, code))
            {
                return false;
            }
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
    
    bool exclusive = (family.place.exclusive && family.place.type != PlaceID::DELEGATE);
    m_raunit.Free(indices, exclusive ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL);
    UpdateContextAvailability();
    
    // Release member threads, if any
    if (family.members.head != INVALID_TID)
    {
        assert(family.place.type != PlaceID::DELEGATE);
        m_threadTable.PushEmpty(family.members, family.place.exclusive ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL);
        UpdateContextAvailability();
    }
    
    if (family.parent.gpid != INVALID_GPID) {
        // Killed a delegated family
        DebugSimWrite("Killed delegated F%u (parent: F%u@CPU%u)", (unsigned)fid, (unsigned)family.parent.fid, (unsigned)family.parent.gpid);
    } else if (family.type == Family::GROUP) {
        // Killed a group family
        DebugSimWrite("Killed group F%u (parent: F%u@P%u)", (unsigned)fid, (unsigned)family.parent.fid, (unsigned)family.parent.lpid);
    } else {
        // Killed a local family
        DebugSimWrite("Killed local F%u", (unsigned)fid);
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
                        DeadlockWrite("Unable to send family termination to F%u on previous processor", (unsigned)family.link_prev);
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
                DeadlockWrite("Unable to kill family F%u", (unsigned)fid);
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
            
            // Free the family table entry
            bool exclusive = (family.place.exclusive && family.place.type != PlaceID::DELEGATE);
            m_familyTable.FreeFamily(fid, exclusive ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL);
            UpdateContextAvailability();
            
            DebugSimWrite("Cleaned up F%u", (unsigned)fid);
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

bool Allocator::DecreaseThreadDependency(TID tid, ThreadDependency dep)
{
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
            if (!m_cleanup.Push(tid))
            {
                return false;
            }
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
void Allocator::SetDefaultFamilyEntry(LFID fid, TID parent, const RegisterBases bases[], const PlaceID& place) const
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
        family.place         = place;
        family.link_prev     = INVALID_LFID;
        family.link_next     = INVALID_LFID;

        // By default, the globals and shareds are taken from the locals of the parent thread
        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            family.regs[i].parent_globals = bases[i].globals;
            family.regs[i].parent_shareds = bases[i].shareds;
        }
    }
}

bool Allocator::IsContextAvailable() const
{
    // Note that we check against 1, and not 0, to keep a local context for sequentialization
    return m_raunit     .GetNumFreeContexts() > 1 &&
           m_threadTable.GetNumFreeThreads()  > 1 &&
           m_familyTable.GetNumFreeFamilies() > 1;
}

// Called whenever a context element is used.
// Checks if a context is still available and updates the global signal accordingly.
void Allocator::UpdateContextAvailability()
{
    if (IsContextAvailable())
    {
        // We are not full
        m_place.m_full_context.Clear(m_lpid);
    }
    else
    {
        // We are full
        m_place.m_full_context.Set(m_lpid);
    }
}

// Allocates a family entry. Returns the LFID (and SUCCESS) if one is available,
// DELAYED if none is available and the request is written to the buffer. FAILED is
// returned if the buffer was full.
Result Allocator::AllocateFamily(TID parent, RegIndex reg, LFID* fid, const RegisterBases bases[], Integer place_id)
{
    // Unpack the place value: <Capability:N, PID:P, EX:1>
    const unsigned int P = (unsigned int)ceil(log2(m_parent.GetGridSize()));

    PlaceID place;
    place.exclusive  = (((place_id >> 0) & 1) != 0);
    place.type       = (PlaceID::Type)((place_id >> 1) & 3);
    place.pid        = (GPID)((place_id >> 3) & ((1ULL << P) - 1));
    place.capability = place_id >> (P + 3);

    if (place.type == PlaceID::DELEGATE)
    {
        if (place.pid >= m_parent.GetGridSize())
        {
            throw SimulationException("Attempting to delegate to a non-existing core");
        }
        
        if (place.pid == m_parent.GetPID())
        {
            // We're delegating to our own core, make it a group create instead
            place.type = PlaceID::GROUP;
        }
    }
    
    if (!p_allocation.Invoke())
    {
        DeadlockWrite("Unable to acquire service for family allocation");
        return FAILED;
    }

    bool exclusive = (place.exclusive && place.type != PlaceID::DELEGATE);
    *fid = m_familyTable.AllocateFamily(exclusive ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL);
    if (*fid != INVALID_LFID)
    {
        // A family entry was free
        UpdateContextAvailability();
        SetDefaultFamilyEntry(*fid, parent, bases, place);
        return SUCCESS;
    }

    // Place the request in the buffer
    AllocRequest request;
    request.parent = parent;
    request.place  = place;
    request.reg    = reg;
    std::copy(bases, bases + NUM_REG_TYPES, request.bases);
    
    Buffer<AllocRequest>& allocations = exclusive ? m_allocationsEx : m_allocations;
    if (!allocations.Push(request))
    {
        return FAILED;
    }
    
    return DELAYED;
}

bool Allocator::OnDelegationFailed(LFID fid)
{
    assert(fid != INVALID_LFID);
    Family& family = m_familyTable[fid];
    assert(family.type == Family::DELEGATED);
    assert(!family.place.exclusive);
    
    // Reinitialize the family to a local family
    COMMIT{ family.place.type = PlaceID::LOCAL; }
    
    // And push it back on the create queue
    if (!m_creates.Push(fid))
    {
        DeadlockWrite("Unable to queue reinitialized create to create queue");
        return false;
    }

    return true;
}

Result Allocator::OnDelegatedCreate(const DelegateMessage& msg)
{
    assert(msg.parent.pid != INVALID_GPID);
    assert(msg.parent.fid != INVALID_LFID);
    
    if (!p_allocation.Invoke())
    {
        DeadlockWrite("Unable to acquire service for family allocation");
        return FAILED;
    }
    
    if (!msg.exclusive && !IsContextAvailable())
    {
        // We cannot get an entry, or use the last entry on this core.
        // Tell the network to send back a denial so the parent core can
        // restart the family locally.
        return DELAYED;
    }
    
    LFID fid = m_familyTable.AllocateFamily(msg.exclusive ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL);
    if (fid == INVALID_LFID)
    {
        DeadlockWrite("Unable to acquire family table entry for delegated create");
        return FAILED;
    }
    UpdateContextAvailability();
    
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
            family.regs[i].parent_globals = INVALID_REG_INDEX;
            family.regs[i].parent_shareds = INVALID_REG_INDEX;
        }
    }
        
    if (!m_creates.Push(fid))
    {
        DeadlockWrite("Unable to queue delegated create to create queue");
        return FAILED;
    }
    
    DebugSimWrite("Queued delegated create by F%u@CPU%u at %s",
                  (unsigned)msg.parent.fid, (unsigned)msg.parent.pid, GetKernel()->GetSymbolTable()[msg.address].c_str());
        
    return SUCCESS;
}

void Allocator::ReserveContext(bool self)
{
    // This should not fail
    if (!self)
    {
        m_familyTable.ReserveFamily();
        m_raunit.ReserveContext();
    }
    m_threadTable.ReserveThread();
    
    UpdateContextAvailability();
}

LFID Allocator::OnGroupCreate(const CreateMessage& msg, LFID link_next)
{
    if (!p_allocation.Invoke())
    {
        DeadlockWrite("Unable to acquire service for family allocation");
        return INVALID_LFID;
    }
    
    LFID fid = m_familyTable.AllocateFamily(CONTEXT_RESERVED);
    if (fid == INVALID_LFID)
    {
        // Couldn't allocate an entry
        return INVALID_LFID;
    }
    UpdateContextAvailability();

    DebugSimWrite("Allocated family F%u for group create", (unsigned)fid);

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
    family.parent.fid    = msg.parent.fid;
    family.virtBlockSize = msg.virtBlockSize;
    family.physBlockSize = msg.physBlockSize;
    family.pc            = msg.address;
    family.hasDependency = false;
    family.link_prev     = msg.link_prev;
    family.link_next     = link_next;
    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
        family.regs[i].parent_globals = INVALID_REG_INDEX;
        family.regs[i].parent_shareds = INVALID_REG_INDEX;
        family.regs[i].count          = msg.regsNo[i];
    }

    // Initialize the family
    InitializeFamily(fid, Family::GROUP);

    // Allocate the registers
    if (!AllocateRegisters(fid, CONTEXT_RESERVED))
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
    if (!p_alloc.Invoke())
    {
        return false;
    }
    
    m_alloc.Push(fid);
    return true;
}

void Allocator::InitializeFamily(LFID fid, Family::Type type) const
{
    /*
     IMPORTANT:
    
     This function may be called twice for a single family when a group or delegated
     family is re-initialized as a local family when it turns out there aren't enough
     contexts available.
    
     So make sure this function is idempotent!
     */
    COMMIT
    {
        Family& family    = m_familyTable[fid];
        PSize   placeSize = m_parent.GetPlaceSize();
        
        family.members.head = INVALID_TID;
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
            if (regs.parent_globals != INVALID_REG_INDEX)
            {
                if (regs.count.globals == 0) {
                    // We have no parent globals
                    regs.parent_globals = INVALID_REG_INDEX;
                } else if (regs.parent_shareds < regs.parent_globals) {
                    // In case globals and shareds overlap, move globals up
                    regs.parent_globals = max(regs.parent_shareds + regs.count.shareds, regs.parent_globals);
                }
            }

            if (regs.parent_shareds != INVALID_REG_INDEX)
            {
                if (regs.count.shareds == 0) {
                    // We have no parent shareds
                    regs.parent_shareds = INVALID_REG_INDEX;
                } else if (regs.parent_globals <= regs.parent_shareds) {
                    // In case globals and shareds overlap, move shareds up
                    regs.parent_shareds = max(regs.parent_globals + regs.count.globals, regs.parent_shareds);
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

bool Allocator::AllocateRegisters(LFID fid, ContextType type)
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
                regs.base = INVALID_REG_INDEX; 
                regs.size = 0; 
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
            // The family needs a cache for remote shareds if
            // * it's a group create (even if we're on the parent), or
            // * if it's a delegated create (to receive from parent)
            const bool need_shareds_cache = (family.type == Family::GROUP || family.parent.gpid != INVALID_GPID);

            // The family needs a cache for remote globals if
            // * it's a group create AND we're not on the parent, or
            // * if it's a delegated create (to receive from parent)
            const bool need_globals_cache = ((family.type == Family::GROUP && family.parent.lpid != m_lpid) || family.parent.gpid != INVALID_GPID);

            const Family::RegInfo& regs = family.regs[i];

            sizes[i] = (regs.count.locals + regs.count.shareds) * physBlockSize;
            if (need_globals_cache) sizes[i] += regs.count.globals; // Add the cache for the globals
            if (need_shareds_cache) sizes[i] += regs.count.shareds; // Add the cache for the remote shareds
        }

        RegIndex indices[NUM_REG_TYPES];
        if (m_raunit.Alloc(sizes, fid, type, indices))
        {
            // Success, we have registers for all types
            COMMIT{ family.physBlockSize = physBlockSize; }
            UpdateContextAvailability();
            
            for (RegType i = 0; i < NUM_REG_TYPES; i++)
            {
                RegIndex base = INVALID_REG_INDEX;
                if (sizes[i] > 0)
                {
                    // Clear the allocated registers
                    m_registerFile.Clear(MAKE_REGADDR(i, indices[i]), sizes[i]);
                    base = indices[i];
                }

                COMMIT
                {
                    Family::RegInfo& regs = family.regs[i];
                    regs.base = base;
                    regs.size = sizes[i];
                }
                DebugSimWrite("%d: Allocated %u registers at 0x%04x", (int)i, (unsigned)sizes[i], (unsigned)indices[i]);
            }
            return true;
        }
    }

    return false;
}

bool Allocator::OnCachelineLoaded(CID cid)
{
    assert(!m_createFID.Empty());
    COMMIT{
        m_createState = CREATE_LINE_LOADED;
        m_createLine  = cid;
    }
    return true;
}


Result Allocator::DoThreadAllocate()
{
    //
    // Cleanup (reallocation) takes precedence over initial allocation
    //
    if (!m_cleanup.Empty())
    {
        TID     tid    = m_cleanup.Front();
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
                if (!DecreaseThreadDependency(thread.nextInBlock, THREADDEP_PREV_CLEANED_UP))
                {
                    DeadlockWrite("Unable to mark PREV_CLEANED_UP for T%u's next thread T%u in F%u",
                        (unsigned)tid, (unsigned)thread.nextInBlock, (unsigned)fid);
                    return FAILED;
                }
            }
            else if (!m_network.SendThreadCleanup(family.link_next))
            {
                DeadlockWrite("Unable to send thread cleanup notification for T%u in F%u",
                    (unsigned)tid, (unsigned)fid);
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
                DeadlockWrite("Unable to decrease thread count during cleanup of T%u in F%u",
                    (unsigned)tid, (unsigned)fid);
                return FAILED;
            }

            DebugSimWrite("Cleaned up T%u for F%u (index %llu)",
                (unsigned)tid, (unsigned)fid, (unsigned long long)thread.index);
            }
        else
        {
            // Reallocate thread
            if (!AllocateThread(fid, tid, false))
            {
                DeadlockWrite("Unable to reallocate thread T%u in F%u",
                    (unsigned)tid, (unsigned)fid);
                return FAILED;
            }

            if (family.dependencies.allocationDone && !m_alloc.Empty() && m_alloc.Front() == fid)
            {
                // Go to next family
                DebugSimWrite("Done allocating from F%u", (unsigned)m_alloc.Front());
                m_alloc.Pop();
            }
        }
        m_cleanup.Pop();
            return SUCCESS;
    }
    
        assert (!m_alloc.Empty());
    {
        // Allocate an initial thread of a family
        LFID    fid    = m_alloc.Front();
        Family& family = m_familyTable[fid];

        // We only allocate from a special pool once:
        // for the first thread of the family.
        bool exclusive = family.dependencies.numThreadsAllocated == 0 && family.place.exclusive;
        bool reserved  = family.dependencies.numThreadsAllocated == 0 && family.type == Family::GROUP;// && family.parent.lpid != m_lpid;
            
        bool done = true;
        if (family.infinite || (family.nThreads > 0 && family.index < family.nThreads))
        {
            // We have threads to run
            
            TID tid = m_threadTable.PopEmpty( exclusive ? CONTEXT_EXCLUSIVE : (reserved ? CONTEXT_RESERVED : CONTEXT_NORMAL) );
            if (tid == INVALID_TID)
            {
                DeadlockWrite("Unable to allocate a free thread entry for F%u", (unsigned)fid);
                return FAILED;
            }
            
            // Update after allocating a thread
            UpdateContextAvailability();
        
            if (!AllocateThread(fid, tid))
            {
                DeadlockWrite("Unable to allocate new thread T%u for F%u", (unsigned)tid, (unsigned)fid);
                return FAILED;
            }

            // Check if we're done with the initial allocation of this family
            done = (family.dependencies.numThreadsAllocated == family.physBlockSize || family.dependencies.allocationDone);
        }
        else
        {
            // We don't have any threads to run
            if (reserved)
            {
                // We've reserved a thread for the create but aren't using it.
                // Unreserve it.
                m_threadTable.UnreserveThread();
            }
            
            if (!DecreaseFamilyDependency(fid, FAMDEP_ALLOCATION_DONE))
            {
                return FAILED;
            }
        }
        
        if (done)
        {
            // We're done with this family
            DebugSimWrite("Done allocating from F%u", (unsigned)fid);
            m_alloc.Pop();
        }
            return SUCCESS;
    }
}

Result Allocator::DoFamilyAllocate()
{
    // Pick an allocation queue to allocate from
    Buffer<AllocRequest>* buffer = NULL;
    if (!m_familyTable.IsExclusiveUsed() && !m_allocationsEx.Empty())
    {
        buffer = &m_allocationsEx;
    }
    else if (!m_allocations.Empty())
    {
        buffer = &m_allocations;
    }

    if (buffer == NULL)
    {
        return FAILED;
    }

    const AllocRequest& req = buffer->Front();
            
    if (!p_allocation.Invoke())
    {
        DeadlockWrite("Unable to acquire service for family allocation");
        return FAILED;
    }
            
    LFID fid = m_familyTable.AllocateFamily(buffer == &m_allocationsEx ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL);
    if (fid == INVALID_LFID)
    {
        DeadlockWrite("Unable to allocate a free family entry (target R%04x)", (unsigned)req.reg);
        return FAILED;
    }
            
    // A family entry was free
    UpdateContextAvailability();
    SetDefaultFamilyEntry(fid, req.parent, req.bases, req.place);

    // Writeback the FID
    RegisterWrite write;
    write.address = MAKE_REGADDR(RT_INTEGER, req.reg);
    write.value.m_state   = RST_FULL;
    write.value.m_integer = fid;
    if (!m_registerWrites.Push(write))
    {
        DeadlockWrite("Unable to queue write to register R%04x", (unsigned)req.reg);
        return FAILED;
    }
    buffer->Pop();
    return SUCCESS;
}

Result Allocator::DoFamilyCreate()
{
    if (m_createState == CREATE_INITIAL)
    {
        assert(m_createFID.Empty());
        assert(!m_creates.Empty());
        
        LFID fid = m_creates.Front();
        m_creates.Pop();

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
        DebugSimWrite("Processing %s create for F%u", create_type, (unsigned)fid);

        // Load the register counts from the family's first cache line
            Instruction counts;
            CID         cid;
            Result      result;
            if ((result = m_icache.Fetch(family.pc - sizeof(Instruction), sizeof(counts), cid)) == FAILED)
            {
                DeadlockWrite("Unable to fetch the I-Cache line for 0x%016llx for F%u", (unsigned long long)family.pc, (unsigned)fid);
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
            
            m_createFID.Write(fid);
            COMMIT{ family.state = FST_CREATING; }
        }
    else if (m_createState == CREATE_LOADING_LINE)
    {
        DeadlockWrite("Waiting for the cache-line to be loaded");
        return FAILED;
    }
        else if (m_createState == CREATE_LINE_LOADED)
        {
            assert(!m_createFID.Empty());
            LFID fid = m_createFID.Read();
            Family& family = m_familyTable[fid];
            
            // Read the cache-line
            Instruction counts;
            if (!m_icache.Read(m_createLine, family.pc - sizeof(Instruction), &counts, sizeof(counts)))
            {
                DeadlockWrite("Unable to read the I-Cache line for 0x%016llx for F%u", (unsigned long long)family.pc, (unsigned)fid);
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
            DeadlockWrite("Unable to release cache line for F%u", (unsigned)fid);
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
            if (!m_network.SendDelegatedCreate(fid))
            {
                    DeadlockWrite("Unable to send the delegation for F%u", (unsigned)fid);
                return FAILED;
            }

            // Reset the create state; we're done with this create
            // Note that if the delegate fails remotely, we push this family
            // back onto the create queue, but change it to a local family.
            COMMIT{ m_createState = CREATE_INITIAL; }
            m_createFID.Clear();
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
                    DeadlockWrite("Unable to request the create token from the network for F%u", (unsigned)fid);
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
        InitializeFamily(fid, type);
    }
    else if (m_createState == CREATE_ACQUIRING_TOKEN)
    {
        DeadlockWrite("Waiting for token to be acquired");
        return FAILED;
    }
        else if (m_createState == CREATE_BROADCASTING_CREATE)
        {
            // We have the token
            assert(!m_createFID.Empty());
            LFID fid = m_createFID.Read();
            
            // See if we can broadcast the create
            if (m_place.m_full_context.IsSet())
            {
                // We cannot do a group create; re-initialize family as a local create
                DebugSimWrite("Reinitializing F%u as a local family due to lack of contexts in group", (unsigned)fid);
                InitializeFamily(fid, Family::LOCAL);
            }
            else
            {
                // There's a context, broadcast the create
                if (!m_network.SendGroupCreate(fid))
                {
                DeadlockWrite("Unable to send the create for F%u", (unsigned)fid);
                    return FAILED;
                }
        }
        
        // We're done with the token
        m_network.ReleaseToken();
        
            // Advance to next stage
            COMMIT{ m_createState = CREATE_ALLOCATING_REGISTERS; }
        }
        else if (m_createState == CREATE_ALLOCATING_REGISTERS)
        {
            assert(!m_createFID.Empty());
            LFID fid = m_createFID.Read();
            
            // Allocate the registers
            const Family& family = m_familyTable[fid];
            if (!AllocateRegisters(fid, family.place.exclusive ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL))
            {
                DeadlockWrite("Unable to allocate registers for F%u", (unsigned)fid);
                return FAILED;
            }

            // Family's now created, we can start creating threads
            if (!ActivateFamily(fid))
            {
                DeadlockWrite("Unable to activate the family F%u", (unsigned)fid);
                return FAILED;
            }

        // Reset the create state
            COMMIT{ m_createState = CREATE_INITIAL; }
            m_createFID.Clear();
    }
        return SUCCESS;
}

Result Allocator::DoThreadActivation()
{
    assert (!m_readyThreads.Empty());
    {
        TID     tid    = m_readyThreads.Front();
        Thread& thread = m_threadTable[tid];

        // Pop the thread
        m_readyThreads.Pop();

        // This thread doesn't have a Thread Instruction Buffer yet,
        // so try to get the cache line
        TID    next = tid;
            CID    cid;
            Result result = SUCCESS;
        if ((result = m_icache.Fetch(thread.pc, sizeof(Instruction), next, cid)) == FAILED)
        {
            // We cannot fetch, abort activation
            DeadlockWrite("Unable to fetch the I-Cache line for 0x%016llx to activate thread T%u",
                (unsigned long long)thread.pc, (unsigned)tid);
            return FAILED;
        }

        COMMIT { thread.cid = cid; }

        if (result != SUCCESS)
        {
            // Request was delayed, link thread into waiting queue
            // Mark the thread as waiting
            COMMIT
            {
                thread.nextState = next;
                thread.state     = TST_WAITING;
            }
        }
            else
            {
            // The thread can be added to the family's active queue
            ThreadQueue tq = {tid, tid};
            if (!QueueActiveThreads(tq))
            {
                DeadlockWrite("Unable to enqueue T%u to the Active Queue", (unsigned)tid);
                return FAILED;
            }
        }
        return SUCCESS;
    }
}

Result Allocator::DoRegWrites()
{
        assert (!m_registerWrites.Empty());
    {
        const RegisterWrite& write = m_registerWrites.Front();
        if (!m_registerFile.p_asyncW.Write(write.address))
        {
            DeadlockWrite("Unable to acquire the port for the queued register write to %s", write.address.str().c_str());
            return FAILED;
        }

        if (!m_registerFile.WriteRegister(write.address, write.value, false))
        {
            DeadlockWrite("Unable to write the queued register write to %s", write.address.str().c_str());
            return FAILED;
        }

        m_registerWrites.Pop();
            return SUCCESS;
    }
}

bool Allocator::OnTokenReceived()
{
    // The network told us we can create this family (group family, local create)
    assert(!m_createFID.Empty());
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
        
        nBlock = std::max<Integer>(nBlock,1);
        step   = family.step;
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
        assert(nBlock > 0);

        family.infinite = (family.step == 0);
        family.step     = step;

        family.physBlockSize = nBlock;
        if (family.virtBlockSize > 0 && !hasDependency && family.virtBlockSize < nBlock)
        {
            // For independent families, use the original virtual block size
            // as physical block size, if it is smaller.
            family.physBlockSize = family.virtBlockSize;
        }
        family.virtBlockSize = nBlock;
        family.nThreads      = nThreads;
    }
    return local;
}

// Locally issued creates
bool Allocator::QueueCreate(LFID fid, MemAddr address, TID parent, RegIndex exitCodeReg)
{
    assert(parent != INVALID_TID);

    Family& family = GetWritableFamilyEntry(fid, parent);

    // Push the create
    if (!m_creates.Push(fid))
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
    }
    
    DebugSimWrite("Queued local create by T%u at %s", (unsigned)parent, GetKernel()->GetSymbolTable()[address].c_str());
    return true;
}

Allocator::Allocator(const string& name, Processor& parent,
    FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
    PlaceInfo& place, LPID lpid, const Config& config)
:
    Object(name, parent),
    m_parent(parent), m_familyTable(familyTable), m_threadTable(threadTable), m_registerFile(registerFile), m_raunit(raunit), m_icache(icache), m_network(network), m_pipeline(pipeline),
    m_place(place), m_lpid(lpid),
    m_alloc         (*parent.GetKernel(), familyTable),
    m_creates       (*parent.GetKernel(), config.getInteger<BufferSize>("LocalCreatesQueueSize",          INFINITE), 3),
    m_registerWrites(*parent.GetKernel(), config.getInteger<BufferSize>("RegisterWritesQueueSize",        INFINITE), 2),
    m_cleanup       (*parent.GetKernel(), config.getInteger<BufferSize>("ThreadCleanupQueueSize",         INFINITE), 4),
    m_allocations   (*parent.GetKernel(), config.getInteger<BufferSize>("FamilyAllocationQueueSize",      INFINITE)),
    m_allocationsEx (*parent.GetKernel(), config.getInteger<BufferSize>("FamilyAllocationExclusiveQueueSize", INFINITE)),
    m_createFID     (*parent.GetKernel()),
    m_createState   (CREATE_INITIAL),
    m_readyThreads  (*parent.GetKernel(), threadTable),

    p_ThreadAllocate  ("thread-allocate",   delegate::create<Allocator, &Allocator::DoThreadAllocate  >(*this) ),
    p_FamilyAllocate  ("family-allocate",   delegate::create<Allocator, &Allocator::DoFamilyAllocate  >(*this) ),
    p_FamilyCreate    ("family-create",     delegate::create<Allocator, &Allocator::DoFamilyCreate    >(*this) ),
    p_ThreadActivation("thread-activation", delegate::create<Allocator, &Allocator::DoThreadActivation>(*this) ),
    p_RegWrites       ("reg-write-queue",   delegate::create<Allocator, &Allocator::DoRegWrites       >(*this) ),
    
    p_allocation    (*this, "p_allocation"),
    p_alloc         (*this, "p_alloc"),
    p_readyThreads  (*this, "p_readyThreads"),
    p_activeThreads (*this, "p_activeThreads"),
    m_activeThreads (*parent.GetKernel(), threadTable)
{
    m_alloc         .Sensitive(p_ThreadAllocate);
    m_creates       .Sensitive(p_FamilyCreate);
    m_registerWrites.Sensitive(p_RegWrites);
    m_cleanup       .Sensitive(p_ThreadAllocate);
    m_allocations   .Sensitive(p_FamilyAllocate);
    m_allocationsEx .Sensitive(p_FamilyAllocate);
    m_createFID     .Sensitive(p_FamilyCreate);
    m_readyThreads  .Sensitive(p_ThreadActivation);
    m_activeThreads .Sensitive(pipeline.p_Pipeline); // Fetch Stage is sensitive on this list
}

void Allocator::AllocateInitialFamily(MemAddr pc, bool legacy)
{
    static const unsigned char InitialRegisters[NUM_REG_TYPES] = {31, 31};

    LFID fid = m_familyTable.AllocateFamily(CONTEXT_NORMAL);
    if (fid == INVALID_LFID)
    {
        throw SimulationException("Unable to create initial family");
    }
    UpdateContextAvailability();

    Family& family = m_familyTable[fid];
    family.start         = 0;
    family.step          = 1;
    family.nThreads      = 1;
    family.virtBlockSize = 1;
    family.physBlockSize = 1;
    family.parent.fid    = INVALID_LFID;
    family.parent.lpid   = m_lpid;
    family.parent.gpid   = INVALID_GPID;
    family.exitCodeReg   = INVALID_REG_INDEX;
    family.created       = true;
    family.legacy        = legacy;
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
        family.regs[i].parent_globals = INVALID_REG_INDEX;
        family.regs[i].parent_shareds = INVALID_REG_INDEX;
    };

    InitializeFamily(fid, Family::LOCAL);

    if (!AllocateRegisters(fid, CONTEXT_NORMAL))
    {
        throw SimulationException("Unable to create initial family");
    }

    m_alloc.Push(fid);
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

TID Allocator::Pop(ThreadQueue& q, TID Thread::*link)
{
    TID tid = q.head;
    if (q.head != INVALID_TID)
    {
        q.head = m_threadTable[tid].*link;
    }
    return tid;
}

void Allocator::Cmd_Help(ostream& out, const vector<string>& /* arguments */) const
{
    out <<
    "The Allocator is where most of the thread and family management takes place.\n"
    "\n"
    "Supported operations:\n"
    "- read <component> [range]\n"
    "  Reads and displays the various queues and registers in the Allocator.\n";
}

void Allocator::Cmd_Read(ostream& out, const vector<string>& /*arguments*/) const
{
    {
        const struct {
            const char*                 type;
            const Buffer<AllocRequest>& queue;
        } Queues[2] = {
            {"Non-exclusive", m_allocations},
            {"Exclusive",     m_allocationsEx},
        };
        
        for (int i = 0; i < 2; ++i)
        {
            const Buffer<AllocRequest>& allocations = Queues[i].queue;
            out << Queues[i].type << " family allocation queue: " << endl;
            if (allocations.Empty())
            {
                out << "Empty" << endl;
            }
            else
            {
                for (Buffer<AllocRequest>::const_iterator p = allocations.begin(); p != allocations.end(); )
                {
                    out << "T" << p->parent << ":R" << hex << uppercase << setw(4) << setfill('0') << p->reg << nouppercase << dec;
                    if (++p != allocations.end()) {
                        out << ", ";
                    }
                }
                out << endl;
            }
            out << endl;
        }
    }

    {
        out << "Thread allocation queue" << endl;
        if (m_alloc.Empty())
        {
            out << "Empty" << endl;
        }
        else
        {
            out << dec;
            for (FamilyList::const_iterator f = m_alloc.begin(); f != m_alloc.end();)
            {
                out << "F" << *f;
                if (++f != m_alloc.end()) {
                    out << ", ";
                }
            }
            out << endl;
        }
        out << endl;
    }

    {
        out << "Create queue: " << dec << endl;
        if (m_creates.Empty())
        {
            out << "Empty" << endl;
        }
        else
        {
            for (Buffer<LFID>::const_iterator p = m_creates.begin(); p != m_creates.end(); )
            {
                out << "F" << *p;
                if (++p != m_creates.end()) {
                    out << ", ";
                }
            }
            out << endl;
            out << "Create state for F" << *m_creates.begin() << ": ";
            switch (m_createState)
            {
                case CREATE_INITIAL:              out << "Initial"; break;
                case CREATE_LOADING_LINE:         out << "Loading cache-line"; break;
                case CREATE_LINE_LOADED:          out << "Cache-line loaded"; break;
                case CREATE_ACQUIRING_TOKEN:      out << "Acquiring token"; break;
                case CREATE_BROADCASTING_CREATE:  out << "Broadcasting create"; break;
                case CREATE_ALLOCATING_REGISTERS: out << "Allocating registers"; break;
            }
            out << endl;
        }
        out << endl;
    }

    {
        out << "Cleanup queue: " << endl;
        if (m_cleanup.Empty())
        {
            out << "Empty" << endl;
        }
        else
        {
            for (Buffer<TID>::const_iterator p = m_cleanup.begin(); p != m_cleanup.end(); )
            {
                out << "T" << *p;
                if (++p != m_cleanup.end()) {
                    out << ", ";
                }
            }
            out << endl;
        }
    }
}

}
