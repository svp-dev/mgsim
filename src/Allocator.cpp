#include "Allocator.h"
#include "Processor.h"
#include "Pipeline.h"
#include "Network.h"
#include "config.h"
#include "symtable.h"
#include "sampling.h"
#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

void Allocator::UpdateStats()
{
    CycleNo cycle = GetKernel()->GetCycleNo();
    CycleNo elapsed = cycle - m_lastcycle;
    m_lastcycle = cycle;
    
    m_curallocex = m_allocationsEx.size();
    
    m_totalallocex += m_curallocex * elapsed;
    m_maxallocex = std::max(m_maxallocex, m_curallocex);       
}

RegAddr Allocator::GetRemoteRegisterAddress(const RemoteRegAddr& addr) const
{
    const Family&          family = m_familyTable[addr.fid.lfid];
    const Family::RegInfo& regs   = family.regs[addr.reg.type];

    assert(family.state != FST_EMPTY);
    
    RegIndex base = INVALID_REG_INDEX;
    switch (addr.type)
    {
        case RRT_GLOBAL:
            base = regs.base + regs.size - regs.count.globals;
            break;
            
        case RRT_LAST_SHARED:
            // Get the last allocated thread's shareds
            assert(regs.last_shareds != INVALID_REG_INDEX);
            base = regs.last_shareds;
            break;
        
        case RRT_NEXT_DEPENDENT:
            // Return the dependent address for the first thread in the block
            assert(family.type == Family::GROUP);
            assert(regs.first_dependents != INVALID_REG_INDEX);

            base = regs.first_dependents;
            break;

        case RRT_FIRST_DEPENDENT:
            // Return the dependent address for the first thread in the family
            // This is simply the base of the family's registers.
            // This is the first allocated thread's dependents.
            base = regs.base;
            break;
            
        case RRT_DETACH:
            assert(false);
            break;
            
        default:
            assert(false);
            break;
    }
    return MAKE_REGADDR(addr.reg.type, base + addr.reg.index);
}

// Administrative function for getting a register's type and thread mapping
TID Allocator::GetRegisterType(LFID fid, RegAddr addr, RegClass* group) const
{
    const Family& family = m_familyTable[fid];
    const Family::RegInfo& regs = family.regs[addr.type];

    const RegIndex globals = regs.base + regs.size - regs.count.globals;
    if (addr.index >= globals && addr.index < globals + regs.count.globals)
    {
        // It's a global
        *group = RC_GLOBAL;
        return INVALID_TID;
    }
    
    for (TID i = 0; i < m_threadTable.GetNumThreads(); ++i)
    {
        const Thread& thread = m_threadTable[i];
        if (thread.state != TST_EMPTY && thread.family == fid)
        {
            const Thread::RegInfo& tregs = m_threadTable[i].regs[addr.type];
            if (tregs.locals != INVALID_REG_INDEX && addr.index >= tregs.locals && addr.index < tregs.locals + regs.count.locals) {
                *group = RC_LOCAL;
                return i;
            }
        
            if (tregs.shareds != INVALID_REG_INDEX && addr.index >= tregs.shareds && addr.index < tregs.shareds + regs.count.shareds) {
                *group = RC_SHARED;
                return i;
            }

            if (tregs.dependents != INVALID_REG_INDEX && addr.index >= tregs.dependents && addr.index < tregs.dependents + regs.count.shareds) {
                *group = RC_DEPENDENT;
                return i;
            }
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

bool Allocator::OnRemoteSync(LFID fid, FCapability capability, GPID remote_pid, RegIndex remote_reg)
{
    assert(remote_pid != INVALID_GPID);
    assert(remote_pid != m_parent.GetPID());
    assert(remote_reg != INVALID_REG_INDEX);
    
    Family& family = GetFamilyChecked(fid, capability);
    
    if (family.sync.code == EXITCODE_NONE)
    {
        // The family hasn't terminated yet, setup sync link
        COMMIT
        {
            family.sync.pid = remote_pid;
            family.sync.reg = remote_reg;
        }
    }
    else
    {
        // The family has already completed, send sync result back
        RemoteMessage msg;
        msg.type = RemoteMessage::MSG_REGISTER;
        msg.reg.addr.type       = RRT_RAW;
        msg.reg.addr.fid.pid    = remote_pid;
        msg.reg.addr.fid.lfid   = INVALID_LFID;
        msg.reg.addr.reg        = MAKE_REGADDR(RT_INTEGER, remote_reg);
        msg.reg.value.m_state   = RST_FULL;
        msg.reg.value.m_integer = family.sync.code;
        
        if (!m_network.SendMessage(msg))
        {
            DeadlockWrite("Unable to send exit code of F%u to R%u@CPU%u", (unsigned)fid, (unsigned)remote_reg, (unsigned)remote_pid);
            return false;
        }
    }
    return true;
}

bool Allocator::OnRemoteThreadCleanup(LFID fid)
{
    Family& family = m_familyTable[fid];
    assert(family.state != FST_EMPTY);

    // The first thread in a block must exist for this notification
    if (family.firstThreadInBlock == INVALID_TID)
    {
        // The first thread does not exist, buffer the event in a flag in the family
        COMMIT{ family.prevCleanedUp = true; }
        DebugSimWrite("Remote thread cleanup for F%u; buffering", (unsigned)fid);
    }
    else
    {
        DebugSimWrite("Remote thread cleanup for F%u; marking T%u", (unsigned)fid, (unsigned)family.firstThreadInBlock);
        if (!DecreaseThreadDependency(family.firstThreadInBlock, THREADDEP_PREV_CLEANED_UP))
        {
            return false;
        }
        COMMIT{ family.firstThreadInBlock = INVALID_TID; }
    }
    
    COMMIT
    {
        // If we have allocated the last thread in the block already, set its shareds

        // as first dependents. Otherwise, clear the first dependents.
        for (RegType i = 0; i < NUM_REG_TYPES; ++i)
        {
            family.regs[i].first_dependents = (family.lastAllocatedIsLastThreadInBlock)
                ? family.regs[i].last_shareds
                : INVALID_REG_INDEX;
            
            DebugSimWrite("Setting first dependents of F%u to %s",
                (unsigned)fid, MAKE_REGADDR(i, family.regs[i].first_dependents).str().c_str());
        }
    }
    
    return true;
}

// This is called by the pipeline to stop allocating threads of local family
bool Allocator::OnLocalBreak(LFID fid)
{
    Family& family = m_familyTable[fid];

    if (!family.dependencies.allocationDone)
    {
        // Stop creation at our current point
        if (family.infinite)
        {
            // Set a finite family limit
            COMMIT
            {
                family.infinite = false;
                family.nThreads = family.index + 1;
            }
            
            DebugSimWrite("For infinite family,set thread number to %u ",
                (unsigned)family.index + 1);
        }
        else
        {
            assert(family.index < family.nThreads);

            DebugSimWrite("For finite family with %u threads,set it to %u ",
                (unsigned)family.nThreads, (unsigned)family.index + 1);

            if (family.index + 1 < family.nThreads)
            {
                // Move the family limit down
                COMMIT{ family.nThreads = family.index + 1; }
            }
        }
    }
    return true;
}

// This is called by network to stop allocating threads of group family
bool Allocator::OnGroupBreak(LFID lfid, Integer index)
{
    Family& family = m_familyTable[lfid];
    assert(index >= family.index);

    // Get the remainder of threads in the block
    Integer remainder = (family.virtBlockSize - (family.index % family.virtBlockSize)) % family.virtBlockSize;

    if (family.infinite)
    {
        COMMIT
        {
            family.infinite = false;
            family.nThreads = family.hasDependency ? index : (family.index + remainder);
        }
        
        DebugSimWrite("Change F%u to be Finite, set its thread number to %u",
            (unsigned)lfid, (unsigned)family.nThreads);
    }
    else if (index < family.nThreads)
    {
        COMMIT
        {
            family.nThreads = family.hasDependency ? index : (family.index + remainder);
        }
        DebugSimWrite("Set thread number of F%u to %u", (unsigned)lfid, (unsigned)family.nThreads);
    }
    return true;
}

//
// This is called by various components (RegisterFile, Pipeline, ...) to
// add the threads to the ready queue.
//
bool Allocator::ActivateThreads(const ThreadQueue& threads)
{
    ThreadList* list;
    if (dynamic_cast<const Pipeline*>(GetKernel()->GetActiveProcess()->GetObject()) != NULL)
    {
        // Request comes from the pipeline, use the first list
        list = &m_readyThreads1;
    }
    else
    {
        // Request comes from something else, arbitrate and use the second list
        if (!p_readyThreads.Invoke())
        {
            DeadlockWrite("Unable to acquire arbitrator for Ready Queue");
            return false;
        }    
        list = &m_readyThreads2;
    }
    
    if (!QueueThreads(*list, threads, TST_READY))
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
    thread->nextInBlock           = INVALID_TID;
    thread->waitingForWrites      = false;
    thread->nextState             = INVALID_TID;

    // Initialize dependencies:
    // These dependencies only hold for non-border threads in dependent families that are global or have more than one thread running.
    // So we already set them in the other cases.
    thread->dependencies.prevCleanedUp    = family->prevCleanedUp || !family->hasDependency || thread->isFirstThreadInFamily || (family->type == Family::LOCAL && family->physBlockSize == 1);
    thread->dependencies.killed           = false;
    thread->dependencies.numPendingWrites = 0;
    
    family->prevCleanedUp = false;

    const bool isFirstThreadInBlock = (family->index % family->virtBlockSize == 0);

    if ((family->type == Family::LOCAL || !isFirstThreadInBlock) && family->physBlockSize > 1 && family->lastAllocated != INVALID_TID)
    {
        COMMIT{ m_threadTable[family->lastAllocated].nextInBlock = tid; }
    }

    // Set the register information for the new thread
    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
        if (isNewlyAllocated)
        {
            // Set the locals for the new thread
            thread->regs[i].locals = family->regs[i].base + (RegIndex)family->dependencies.numThreadsAllocated * (family->regs[i].count.shareds + family->regs[i].count.locals) + family->regs[i].count.shareds;
            
            // The first allocated thread's shareds lie after the locals
            thread->regs[i].shareds = thread->regs[i].locals + family->regs[i].count.locals;
        }
        else
        {
            // When reusing a thread slot, use the old dependents as shareds
            thread->regs[i].shareds = thread->regs[i].dependents;
        }
        
        // This thread's dependents are the last allocated thread's shareds
        thread->regs[i].dependents = family->regs[i].last_shareds;
        
        // Update the family's last allocated shareds
        family->regs[i].last_shareds = thread->regs[i].shareds;
        
        DebugSimWrite("Allocated thread; %u dependents at %s, %u locals at %s, %u shareds at %s",
            family->regs[i].count.shareds, MAKE_REGADDR(i, thread->regs[i].dependents).str().c_str(),
            family->regs[i].count.locals,  MAKE_REGADDR(i, thread->regs[i].locals)    .str().c_str(),
            family->regs[i].count.shareds, MAKE_REGADDR(i, thread->regs[i].shareds)   .str().c_str() );
    }

    COMMIT
    {
        // Reserve the memory (commits on use)
        const MemAddr tls_base = m_parent.GetTLSAddress(fid, tid);
        const MemSize tls_size = m_parent.GetTLSSize();
        m_parent.MapMemory(tls_base+tls_size/2, tls_size/2);
    }

    // Write L0 to the register file
    if (family->regs[RT_INTEGER].count.locals > 0)
    {
        RegAddr  addr = MAKE_REGADDR(RT_INTEGER, thread->regs[RT_INTEGER].locals);
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

    if (isNewlyAllocated)
    {
        assert(family->dependencies.numThreadsAllocated < family->physBlockSize);

        // Add the thread to the family's member queue
        thread->nextMember = INVALID_TID;
        Push(family->members, tid, &Thread::nextMember);

        // Increase the allocation count
        family->dependencies.numThreadsAllocated++;
    }

    //
    // Update family information
    //
    if (isFirstThreadInBlock)
    {
        family->firstThreadInBlock = tid;
        
        DebugSimWrite("Set first thread in block for F%u to T%u (index %llu)", (unsigned)fid, (unsigned)tid, (unsigned long long)thread->index);   
    }
    
    if (thread->isLastThreadInBlock)
    {
        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            if (family->regs[i].first_dependents == INVALID_REG_INDEX)
            {
                family->regs[i].first_dependents = thread->regs[i].shareds;

                DebugSimWrite("Setting first dependents of F%u to %s",
                    (unsigned)fid, MAKE_REGADDR(i, family->regs[i].first_dependents).str().c_str());
            }
        }
    }
    
    family->lastAllocated = tid;
    family->lastAllocatedIsLastThreadInBlock = thread->isLastThreadInBlock;

    // Increase the index and counts
    if (thread->isLastThreadInFamily)
    {
        // We've allocated the last thread
        if (!DecreaseFamilyDependency(fid, *family, FAMDEP_ALLOCATION_DONE))
        {
            DeadlockWrite("Unable to mark ALLOCATION_DONE in family");
            return false;
        }

        family->hasLastThread = true;
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
                DeadlockWrite("Unable to mark ALLOCATION_DONE in family");
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

bool Allocator::SynchronizeFamily(LFID fid, Family& family)
{
    assert(family.sync.code != EXITCODE_NONE);
    
    if (family.parent_lpid != m_lpid)
    {
        // Send synchronization to next processor
        if (!m_network.SendFamilySynchronization(family.link_next))
        {
            return false;
        }
    }
    // This is the parent CPU of the family. All other CPUs have
    // finished and synched. Write back the exit code.
    else if (family.sync.pid != INVALID_GPID)
    {
        // A thread is synching on this family
        assert(family.sync.reg != INVALID_REG_INDEX);
            
        if (family.sync.pid != m_parent.GetPID())
        {
            // Remote thread, send a remote register write
            RemoteMessage msg;
            msg.type = RemoteMessage::MSG_REGISTER;
            msg.reg.addr.type       = RRT_RAW;
            msg.reg.addr.fid.pid    = family.sync.pid;
            msg.reg.addr.fid.lfid   = INVALID_LFID;
            msg.reg.addr.reg        = MAKE_REGADDR(RT_INTEGER, family.sync.reg);
            msg.reg.value.m_state   = RST_FULL;
            msg.reg.value.m_integer = family.sync.code;
            
            if (!m_network.SendMessage(msg))
            {
                return false;
            }
        }
        else
        {
            // Local thread
            RegisterWrite write;
            write.address = family.sync.reg;
            write.value   = family.sync.code;
            if (!m_registerWrites.Push(write))
            {
                return false;
            }
        }
    }

    // Release member threads, if any
    if (family.members.head != INVALID_TID)
    {
        m_threadTable.PushEmpty(family.members, m_familyTable.IsExclusive(fid) ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL);
        UpdateContextAvailability();
    }
    
    DebugSimWrite("Killed F%u", (unsigned)fid);
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
    case FAMDEP_THREAD_COUNT:      assert(deps->numThreadsAllocated > 0); deps->numThreadsAllocated--;  break;
    case FAMDEP_OUTSTANDING_READS: assert(deps->numPendingReads     > 0); deps->numPendingReads    --;  break;
    case FAMDEP_PREV_SYNCHRONIZED: assert(!deps->prevSynchronized);       deps->prevSynchronized = true; break;
    case FAMDEP_ALLOCATION_DONE:   assert(!deps->allocationDone);         deps->allocationDone   = true; break;
    case FAMDEP_DETACHED:          assert(!deps->detached);               deps->detached         = true; break;
    case FAMDEP_BREAKED:           assert(!deps->breaked);                deps->breaked          = true; break;
    }

    switch (dep)
    {
    case FAMDEP_THREAD_COUNT:
    case FAMDEP_ALLOCATION_DONE:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone)
        {
            // It's considered 'killed' when all threads have gone
            COMMIT
            {
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
        if (deps->numThreadsAllocated == 0 && deps->allocationDone &&
            deps->prevSynchronized)
        {
            // Forward synchronization token
            family.sync.code = EXITCODE_NORMAL;
            if (!SynchronizeFamily(fid, family))
            {
                DeadlockWrite("Unable to kill family F%u", (unsigned)fid);
                return false;
            }
        }
        // Fall through

    case FAMDEP_DETACHED:
    case FAMDEP_OUTSTANDING_READS:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone &&
            deps->numPendingReads     == 0 && deps->detached       &&
            deps->prevSynchronized)
        {
            ContextType context = m_familyTable.IsExclusive(fid) ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL;

            // Release registers
            RegIndex indices[NUM_REG_TYPES];
            for (RegType i = 0; i < NUM_REG_TYPES; i++)
            {
                indices[i] = family.regs[i].base;
            }
            m_raunit.Free(indices, context);
   
            // Free the family table entry
            m_familyTable.FreeFamily(fid, context);
            
            UpdateContextAvailability();
            
            DebugSimWrite("Cleaned up F%u", (unsigned)fid);
        }
        break;
        
	case FAMDEP_BREAKED:
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
    case THREADDEP_TERMINATED:
        if (deps->numPendingWrites == 0 && deps->prevCleanedUp && deps->killed)
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
        case THREADDEP_TERMINATED:         assert(0); break;
        }
    }
    return true;
}

Family& Allocator::GetFamilyChecked(LFID fid, FCapability capability) const
{
    if (fid >= m_familyTable.GetFamilies().size())
    {
        throw InvalidArgumentException(*this, "Invalid Family ID: index out of range");
    }
    
    Family& family = m_familyTable[fid];
    if (family.state == FST_EMPTY)
    {
        throw InvalidArgumentException(*this, "Invalid Family ID: family entry is empty");
    }
    
    if (capability != family.capability)
    {
        if (fid == 0 && capability == 0)
            throw InvalidArgumentException(*this, "Invalid use of Family ID after allocation failure");
        else
            throw InvalidArgumentException(*this, "Invalid Family ID: capability mismatch");
    }

    return family;    
}

void Allocator::ReinitializeFamily(LFID fid, Family::Type type) const
{
    COMMIT
    {
        Family& family = m_familyTable[fid];
        family.type = type;
        switch (type)
        {
        case Family::GROUP:
        {
            // This core will execute the first thread in the family.
            // The +1 will ensure that the first thread runs on the core next
            // to the parent core.
            const PSize placeSize = m_parent.GetPlaceSize();
            const LPID  first_pid = (family.parent_lpid + 1) % placeSize;

            family.dependencies.prevSynchronized = (m_lpid == first_pid);
            
            // The index that the family starts at depends on the position of the core in the ring
            // relative to the processor that will run the first thread.
            family.index = ((placeSize + m_lpid - first_pid) % placeSize) * family.virtBlockSize;
            break;
        }

        case Family::LOCAL:
            family.dependencies.prevSynchronized = true;
            family.index = 0;
            break;
            
        default:
            assert(false);
            break;        
        }
    }
}

// Initializes the family entry with default values.
FCapability Allocator::InitializeFamily(LFID fid, PlaceType place) const
{
    // Capability + FID + PID must fit in an integer word
    FCapability capability = m_parent.GenerateFamilyCapability();
    
    COMMIT
    {
        Family& family = m_familyTable[fid];
        
        family.capability    = capability;
        family.legacy        = false;
        family.start         = 0;
        family.limit         = 1;
        family.step          = 1;
        family.virtBlockSize = 0;
        family.physBlockSize = 0;
        family.parent_lpid   = m_lpid;
        family.parent_lfid   = fid;
        family.place         = place;
        family.link_prev     = INVALID_LFID;
        family.link_next     = INVALID_LFID;
        family.sync.code     = EXITCODE_NONE;
        family.sync.pid      = INVALID_GPID;
        family.hasLastThread = false;        
        family.members.head  = INVALID_TID;
        family.hasDependency      = false;
        family.firstThreadInBlock = INVALID_TID;
        family.lastAllocated      = INVALID_TID;
        family.prevCleanedUp      = false;
        family.lastAllocatedIsLastThreadInBlock = false;

        // Dependencies
        family.dependencies.allocationDone      = false;
        family.dependencies.numPendingReads     = 0;
        family.dependencies.numThreadsAllocated = 0;
        family.dependencies.detached            = false;
        family.dependencies.breaked             = false;
        
        Family::Type type;
        switch (place)
        {
        case PLACE_GROUP:
        {
            type = Family::GROUP;
            if (m_parent.GetPlaceSize() == 1)
            {
                // Force to local, this'll simplify things
                type = Family::LOCAL;
            }
            break;
        }

        case PLACE_LOCAL:
            type = Family::LOCAL;
            break;
            
        default:
            assert(false);
            type = Family::LOCAL;
            break;
        }

        ReinitializeFamily(fid, type);
    }
    
    return capability;
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

/**
 \brief Allocates and initializes a family entry.
 \details This method is used by the pipeline to implement the Allocate
    instruction and by the network to implement remote allocations.
 \param[in] place The place information for the allocate.
 \param[in] src The source processor of the allocate request.
 \param[in] reg The index of the integer register to write the FID back to, if no entry could
                be allocated and the request is suspended.
 \param[in] fid Pointer to an FID variable that will be filled if an entry is allocated.
 \returns SUCCESS if an entry could be allocated. DELAYED if the request has been successfully
    queued and FAILED if the process failed.
*/
Result Allocator::AllocateFamily(const PlaceID& place, GPID src, RegIndex reg, FID* fid)
{
    assert(place.type == PLACE_LOCAL || place.type == PLACE_GROUP);
    
    if (!p_allocation.Invoke())
    {
        DeadlockWrite("Unable to acquire service for family allocation");
        return FAILED;
    }

    LFID lfid = INVALID_LFID;
    if (place.exclusive)
    {
        lfid = m_familyTable.AllocateFamily(CONTEXT_EXCLUSIVE);
    }
    else if (IsContextAvailable())
    {
        // A context was available
        lfid = m_familyTable.AllocateFamily(CONTEXT_NORMAL);
        assert (lfid != INVALID_LFID);

        m_raunit.ReserveContext();
        m_threadTable.ReserveThread();

        UpdateContextAvailability();
    }
    
    if (lfid != INVALID_LFID)
    {
        // Construct a global FID for this family
        fid->pid        = m_parent.GetPID();
        fid->lfid       = lfid;
        fid->capability = InitializeFamily(lfid, place.type);
        return SUCCESS;
    }

    if (!place.suspend)
    {
        // No family entry was available and we don't want to suspend until one is
        // Return 0 in *fid to indicate failure.
        fid->pid        = 0;
        fid->lfid       = 0;
        fid->capability = 0;
        return SUCCESS;
    }
    
    // Place the request in the buffer
    // This buffer only deals with group or (exclusive) local creates.
    AllocRequest request;
    request.place = place.type;
    request.pid   = src;
    request.reg   = reg;
    
    Buffer<AllocRequest>& allocations = place.exclusive ? m_allocationsEx : m_allocations;
    if (place.exclusive) UpdateStats();
    if (!allocations.Push(request))
    {
        return FAILED;
    }
    return DELAYED;
}

bool Allocator::OnDelegatedCreate(const RemoteCreateMessage& msg, GPID remote_pid)
{
    assert(remote_pid     != INVALID_GPID);
    assert(msg.completion != INVALID_REG_INDEX);
    assert(msg.fid.pid    == m_parent.GetPID());
    
    Family& family = GetFamilyChecked(msg.fid.lfid, msg.fid.capability);
    
    // Set PC and lock family
    COMMIT
    {
        family.pc      = msg.address;
        family.state   = FST_CREATE_QUEUED;
    }
    
    // Delegated creates are local
    ReinitializeFamily(msg.fid.lfid, Family::LOCAL);
    
    // Queue the create
    CreateInfo info;
    info.fid        = msg.fid.lfid;
    info.pid        = remote_pid;
    info.completion = msg.completion;
    if (!m_creates.Push(info))
    {
        DeadlockWrite("Unable to queue delegated create to create queue");
        return false;
    }
    
    DebugSimWrite("Queued delegated create at %s", GetKernel()->GetSymbolTable()[msg.address].c_str());
    return true;
}

void Allocator::ReserveContext()
{
    // This should not fail
    m_familyTable.ReserveFamily();
    m_raunit.ReserveContext();
    m_threadTable.ReserveThread();    
    UpdateContextAvailability();
}

LFID Allocator::OnGroupCreate(const GroupCreateMessage& msg)
{
    if (!p_allocation.Invoke())
    {
        DeadlockWrite("Unable to acquire service for family allocation");
        return INVALID_LFID;
    }
    
    // Since the entry is reserved, this should not fail
    LFID fid = m_familyTable.AllocateFamily(CONTEXT_RESERVED);    
    assert(fid != INVALID_LFID);

    UpdateContextAvailability();
    InitializeFamily(fid, PLACE_GROUP);    

    DebugSimWrite("Allocated family F%u for group create", (unsigned)fid);

    // Copy the data
    Family& family = m_familyTable[fid];
    family.infinite      = msg.infinite;
    family.start         = msg.start;
    family.step          = msg.step;
    family.nThreads      = msg.nThreads;
    family.parent_lpid   = msg.parent_lpid;
    family.parent_lfid   = msg.first_fid;
    family.virtBlockSize = msg.virtBlockSize;
    family.physBlockSize = msg.physBlockSize;
    family.pc            = msg.address;
    family.link_prev     = msg.link_prev;
    family.link_next     = INVALID_LFID;
    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
        family.regs[i].count = msg.regsNo[i];
        if (msg.regsNo[i].shareds > 0)
        {
            family.hasDependency = true;
        }
    }
    
    ReinitializeFamily(fid, Family::GROUP);
    
    // Allocate the registers.
    // Since the context is reserved, this should not fail.
    bool success = AllocateRegisters(fid, CONTEXT_RESERVED);    
    assert(success);

    COMMIT{ family.state = FST_CREATING; }
    return fid;
}

bool Allocator::ActivateFamily(LFID fid)
{
    if (!p_alloc.Invoke())
    {
        return false;
    }

    // Update the family state
    COMMIT
    {
        Family& family = m_familyTable[fid];
        assert(family.state == FST_CREATING);
        family.state = FST_ACTIVE;
    }
    
    m_alloc.Push(fid);
    return true;
}

bool Allocator::AllocateRegisters(LFID fid, ContextType type)
{
    // Try to allocate registers
    Family& family = m_familyTable[fid];
    assert(family.physBlockSize > 0);

    for (FSize physBlockSize = min(family.physBlockSize, m_threadTable.GetNumThreads()); physBlockSize > 0; physBlockSize--)
    {
        // Calculate register requirements
        RegSize sizes[NUM_REG_TYPES];
        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            const Family::RegInfo& regs = family.regs[i];
            
            sizes[i] = (regs.count.locals + regs.count.shareds) * physBlockSize
                     + regs.count.globals  // Add the space for the globals
                     + regs.count.shareds; // Add the space for the initial/remote shareds
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
                    regs.base             = base;
                    regs.size             = sizes[i];                    
                    regs.last_shareds     = base;
                    regs.first_dependents = (family.index == 0) ? INVALID_REG_INDEX : base;
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
    assert(!m_creates.Empty());
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

        // Clear the thread's dependents, if any
        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            if (family.regs[i].count.shareds > 0)
            {
                if (!m_registerFile.Clear(MAKE_REGADDR(i, thread.regs[i].dependents), family.regs[i].count.shareds))
                {
                    DeadlockWrite("Unable to clear the thread's dependents");
                    return FAILED;
                }
            }
        }

        if (family.hasDependency && !thread.isLastThreadInFamily && (family.type == Family::GROUP || family.physBlockSize > 1))
        {
            // Mark 'previous thread cleaned up' on the next thread
            if (!thread.isLastThreadInBlock)
            {
                if (thread.nextInBlock == INVALID_TID)
                {
                    COMMIT{ family.prevCleanedUp = true; }
                    DebugSimWrite("Buffering PREV_CLEANED_UP in F%u", (unsigned)fid);
                }
                else if (!DecreaseThreadDependency(thread.nextInBlock, THREADDEP_PREV_CLEANED_UP))
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
            const MemAddr tls_base = m_parent.GetTLSAddress(fid, tid);
            const MemSize tls_size = m_parent.GetTLSSize();
            m_parent.UnmapMemory(tls_base+tls_size/2, tls_size/2);
        }
		
		if( family.nThreads <= family.index && !family.infinite && !family.dependencies.allocationDone)
		{
			if (!DecreaseFamilyDependency(fid, FAMDEP_ALLOCATION_DONE))
			{
				DeadlockWrite("Unable to mark ALLOCATION_DONE for F%u",(unsigned)fid);
				return FAILED;
			}
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
        bool exclusive = family.dependencies.numThreadsAllocated == 0 && m_familyTable.IsExclusive(fid);
        bool reserved  = family.dependencies.numThreadsAllocated == 0;
            
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
            if (reserved && !exclusive)
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
        DeadlockWrite("Exclusive create in process");
        return FAILED;
    }

    const AllocRequest& req = buffer->Front();
            
    if (!p_allocation.Invoke())
    {
        DeadlockWrite("Unable to acquire service for family allocation");
        return FAILED;
    }
            
    LFID lfid = INVALID_LFID;
    if (buffer == &m_allocationsEx)
    {
        lfid = m_familyTable.AllocateFamily(CONTEXT_EXCLUSIVE);
    }
    else if (IsContextAvailable())
    {
        // A context was available
        lfid = m_familyTable.AllocateFamily(CONTEXT_NORMAL);
        assert (lfid != INVALID_LFID);

        m_raunit.ReserveContext();
        m_threadTable.ReserveThread();

        UpdateContextAvailability();
    }

    if (lfid == INVALID_LFID)
    {
        DeadlockWrite("Unable to allocate a free context (target R%04x)", (unsigned)req.reg);
        return FAILED;
    }
            
    FID fid;
    fid.lfid       = lfid;
    fid.pid        = m_parent.GetPID();
    fid.capability = InitializeFamily(lfid, req.place);

    if (req.pid == m_parent.GetPID())
    {
        // Writeback the FID to the local register file
        RegAddr addr = MAKE_REGADDR(RT_INTEGER, req.reg);
        RegValue value;
        value.m_state = RST_FULL;
        value.m_integer = m_parent.PackFID(fid);
        
        if (!m_registerFile.p_asyncW.Write(addr))
        {
            DeadlockWrite("Unable to acquire port to write allocation result to %s", addr.str().c_str());
            return FAILED;
        }
        
        if (!m_registerFile.WriteRegister(addr, value, false))
        {
            DeadlockWrite("Unable to write allocation result to %s", addr.str().c_str());
            return FAILED;
        }
    }
    else
    {
        // Send a message to do a remote writeback
        RemoteMessage msg;
        msg.type = RemoteMessage::MSG_REGISTER;
        msg.reg.addr.type       = RRT_RAW;
        msg.reg.addr.fid.pid    = req.pid;
        msg.reg.addr.fid.lfid   = INVALID_LFID;
        msg.reg.addr.reg        = MAKE_REGADDR(RT_INTEGER, req.reg);
        msg.reg.value.m_state   = RST_FULL;
        msg.reg.value.m_integer = m_parent.PackFID(fid);
        
        if (!m_network.SendMessage(msg))
        {
            DeadlockWrite("Unable to send remote allocation writeback");
            return FAILED;
        }
    }
    UpdateStats();
    buffer->Pop();
    return SUCCESS;
}

Result Allocator::DoFamilyCreate()
{
    // The create at the front of the queue is the current create
    assert(!m_creates.Empty());
    const CreateInfo& info = m_creates.Front();
    
    if (m_createState == CREATE_INITIAL)
    {
        Family& family = m_familyTable[info.fid];
            
        // Determine exclusiveness
        DebugSimWrite("Processing create for F%u", (unsigned)info.fid);

        // Load the register counts from the family's first cache line
        Instruction counts;
        CID         cid;
        Result      result;
        if ((result = m_icache.Fetch(family.pc - sizeof(Instruction), sizeof(counts), cid)) == FAILED)
        {
            DeadlockWrite("Unable to fetch the I-Cache line for 0x%016llx for F%u", (unsigned long long)family.pc, (unsigned)info.fid);
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
        
        COMMIT{ family.state = FST_CREATING; }
    }
    else if (m_createState == CREATE_LOADING_LINE)
    {
        DeadlockWrite("Waiting for the cache-line to be loaded");
        return FAILED;
    }
    else if (m_createState == CREATE_LINE_LOADED)
    {
        Family& family = m_familyTable[info.fid];
            
        // Read the cache-line
        Instruction counts;
        if (!m_icache.Read(m_createLine, family.pc - sizeof(Instruction), &counts, sizeof(counts)))
        {
            DeadlockWrite("Unable to read the I-Cache line for 0x%016llx for F%u", (unsigned long long)family.pc, (unsigned)info.fid);
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

            COMMIT
            {
                if (regcounts[i].globals + 2 * regcounts[i].shareds + regcounts[i].locals > 31)
                {
                    DebugSimWrite("Invalid register counts: %d %d %d\n", (int)regcounts[i].globals, (int)regcounts[i].shareds, (int)regcounts[i].locals);
                    throw InvalidArgumentException(*this, "Too many registers specified in thread body");
                }
            }
        }
        
        // Release the cache-lined held by the create so far
        if (!m_icache.ReleaseCacheLine(m_createLine))
        {
            DeadlockWrite("Unable to release cache line for F%u", (unsigned)info.fid);
            return FAILED;
        }
            
        COMMIT
        {
            for (RegType i = 0; i < NUM_REG_TYPES; i++)
            {
                family.regs[i].base  = INVALID_REG_INDEX;
                family.regs[i].count = regcounts[i];
            }
            family.hasDependency = hasDependency;
        }
        
        // For local and group creates, sanitize the family
        SanitizeFamily(family, hasDependency);
        
        ReinitializeFamily(info.fid, family.type);
            
        // Advance to next stage
        if (family.type == Family::GROUP)
        {
            COMMIT{ m_createState = CREATE_ACQUIRE_TOKEN; }
        }
        else
        {
            assert(family.type == Family::LOCAL);            
            COMMIT{ m_createState = CREATE_ALLOCATING_REGISTERS; }
        }
    }
    else if (m_createState == CREATE_ACQUIRE_TOKEN)
    {
        // Group create, request the create token
        if (!m_network.RequestToken())
        {
            DeadlockWrite("Unable to request the create token from the network for F%u", (unsigned)info.fid);
            return FAILED;
        }

        // Advance to next stage
        COMMIT{ m_createState = CREATE_ACQUIRING_TOKEN; }
    }
    else if (m_createState == CREATE_ACQUIRING_TOKEN)
    {
        DeadlockWrite("Waiting for token to be acquired");
        return FAILED;
    }
    else if (m_createState == CREATE_BROADCASTING_CREATE)
    {
        // We have the token
        // See if we can broadcast the create
        if (m_place.m_full_context.IsSet())
        {
            // We cannot do a group create; re-initialize family as a local create
            DebugSimWrite("Reinitializing F%u as a local family due to lack of contexts in group", (unsigned)info.fid);
            
            ReinitializeFamily(info.fid, Family::LOCAL);
        }
        else
        {
            // There's a context, broadcast the create
            if (!m_network.SendGroupCreate(info.fid, info.completion))
            {
                DeadlockWrite("Unable to send the create for F%u", (unsigned)info.fid);
                return FAILED;
            }
        }
        
        // We're done with the token
        if (!m_network.ReleaseToken())
        {
            DeadlockWrite("Unable to release the token");
            return FAILED;
        }
        
        // Advance to next stage
        COMMIT{ m_createState = CREATE_ALLOCATING_REGISTERS; }
    }
    else if (m_createState == CREATE_ALLOCATING_REGISTERS)
    {
        // Allocate the registers
        const Family& family = m_familyTable[info.fid];
        if (!AllocateRegisters(info.fid, m_familyTable.IsExclusive(info.fid) ? CONTEXT_EXCLUSIVE : CONTEXT_RESERVED))
        {
            DeadlockWrite("Unable to allocate registers for F%u", (unsigned)info.fid);
            return FAILED;
        }
        
        // Advance to next stage
        COMMIT{ m_createState = CREATE_ACTIVATING_FAMILY; }
    }
    else if (m_createState == CREATE_ACTIVATING_FAMILY)
    {
        const Family& family = m_familyTable[info.fid];
        if (family.type != Family::GROUP)
        {
            // Local family; we can start creating threads
            if (!ActivateFamily(info.fid))
            {
                DeadlockWrite("Unable to activate the family F%u", (unsigned)info.fid);
                return FAILED;
            }
        }
        
        // Advance to next stage
        COMMIT{ m_createState = CREATE_NOTIFY; }
    }
    else if (m_createState == CREATE_NOTIFY)
    {
        const Family& family = m_familyTable[info.fid];
        if (family.type == Family::LOCAL)
        {
            FID fid;
            fid.capability = family.capability;
            fid.pid        = m_parent.GetPID();
            fid.lfid       = info.fid;
                
            // Notify parent of create completion
            if (info.pid != m_parent.GetPID())
            {
                // This was a delegated create, send completion back
                // in the form of a remote register write.
                RemoteMessage msg;
                msg.type = RemoteMessage::MSG_REGISTER;
                msg.reg.addr.type       = RRT_RAW;
                msg.reg.addr.fid.pid    = info.pid;
                msg.reg.addr.fid.lfid   = INVALID_LFID;
                msg.reg.addr.reg        = MAKE_REGADDR(RT_INTEGER, info.completion);
                msg.reg.value.m_state   = RST_FULL;
                msg.reg.value.m_integer = m_parent.PackFID(fid);
                
                if (!m_network.SendMessage(msg))
                {
                    DeadlockWrite("Unable to create result for F%u to parent core", (unsigned)info.fid);
                    return FAILED;
                }
            }
            else
            {
                // Local create
                RegAddr addr = MAKE_REGADDR(RT_INTEGER, info.completion);
                RegValue value;
                value.m_state = RST_FULL;
                value.m_integer = m_parent.PackFID(fid);

                if (!m_registerFile.p_asyncW.Write(addr))
                {
                    DeadlockWrite("Unable to acquire port to write create completion to %s", addr.str().c_str());
                    return FAILED;
                }

                if (!m_registerFile.WriteRegister(addr, value, false))
                {
                    DeadlockWrite("Unable to write create completion to %s", addr.str().c_str());
                    return FAILED;
                }
            }
        }
        
        // Reset the create state
        COMMIT{ m_createState = CREATE_INITIAL; }
        m_creates.Pop();
    }
    return SUCCESS;
}

Result Allocator::DoThreadActivation()
{
    TID tid;
    if (!m_readyThreads1.Empty()) {
        tid = m_readyThreads1.Front();
        m_readyThreads1.Pop();
    } else {
        assert(!m_readyThreads2.Empty());
        tid = m_readyThreads2.Front();
        m_readyThreads2.Pop();
    }
    
    {
        Thread& thread = m_threadTable[tid];

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

        COMMIT{ thread.cid = cid; }

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
    const RegisterWrite& write = m_registerWrites.Front();
    const RegAddr        addr  = MAKE_REGADDR(RT_INTEGER, write.address);
    if (!m_registerFile.p_asyncW.Write(addr))
    {
        DeadlockWrite("Unable to acquire the port for the queued register write to %s", addr.str().c_str());
        return FAILED;
    }

    // First read the register, we need to wait until it's Pending
    RegValue value;
    if (!m_registerFile.ReadRegister(addr, value))
    {
        DeadlockWrite("Unable to read the queued register from %s", addr.str().c_str());
        return FAILED;
    }    
    
    if (value.m_state == RST_FULL)
    {
        DeadlockWrite("%s is still full; stalling", addr.str().c_str());
        return FAILED;
    }
    assert(value.m_state == RST_PENDING || value.m_state == RST_WAITING);
    
    value.m_state   = RST_FULL;
    value.m_integer = write.value;
    if (!m_registerFile.WriteRegister(addr, value, false))
    {
        DeadlockWrite("Unable to write the queued register write to %s", addr.str().c_str());
        return FAILED;
    }

    m_registerWrites.Pop();
    return SUCCESS;
}

bool Allocator::OnTokenReceived()
{
    // The network told us we can create this family (group family, local create)
    assert(!m_creates.Empty());
    assert(m_createState == CREATE_ACQUIRING_TOKEN);
    COMMIT{ m_createState = CREATE_BROADCASTING_CREATE; }
    return true;
}

// Sanitizes the limit and block size.
// Use only for non-delegated creates.
void Allocator::SanitizeFamily(Family& family, bool hasDependency)
{
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
            nBlock = (family.type == Family::GROUP)
                ? (nThreads + placeSize - 1) / placeSize  // Group family, divide threads evenly (round up)
                : nThreads;                               // Local family, take as many as possible
        }
        else
        {
            nBlock = family.virtBlockSize;
        }

        nBlock = std::min<Integer>(nBlock, nThreads);
        nBlock = std::max<Integer>(nBlock, 1);
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
}

/**
 \brief Queues local or group creates
 \details Locally issues creates are queued from the pipeline into a buffer.
    A hardware process creates families from this buffer. When a family's
    resources have been created, the completion register is written.
 \param[in] fid        The local FID to queue.
 \param[in] address    The address of the code of threads of the new family.
 \param[in] completion The register that should be written when the family's
                       resources have been created.
*/
bool Allocator::QueueCreate(const FID& fid, MemAddr address, RegIndex completion)
{
    assert(fid.pid == m_parent.GetPID());
    
    Family& family = GetFamilyChecked(fid.lfid, fid.capability);

    COMMIT
    {
        // Store the information
        family.pc    = (address & -(MemAddr)m_icache.GetLineSize()) + 2 * sizeof(Instruction); // Skip control and reg count words
        family.state = FST_CREATE_QUEUED;
    }
    
    // Push the create
    CreateInfo info;
    info.fid        = fid.lfid;
    info.pid        = m_parent.GetPID();
    info.completion = completion;
    if (!m_creates.Push(info))
    {
        return false;
    }
    
    DebugSimWrite("Queued local create for F%u at %s", (unsigned)fid.lfid, GetKernel()->GetSymbolTable()[address].c_str());
    return true;
}

Allocator::Allocator(const string& name, Processor& parent, Clock& clock,
    FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, Network& network, Pipeline& pipeline,
    PlaceInfo& place, LPID lpid, const Config& config)
:
    Object(name, parent, clock),
    m_parent(parent), m_familyTable(familyTable), m_threadTable(threadTable), m_registerFile(registerFile), m_raunit(raunit), m_icache(icache), m_network(network), m_pipeline(pipeline),
    m_place(place), m_lpid(lpid),
    m_alloc         (clock, config.getInteger<BufferSize>("NumFamilies", 8)),
    m_creates       (clock, config.getInteger<BufferSize>("LocalCreatesQueueSize",          INFINITE), 3),
    m_registerWrites(clock, config.getInteger<BufferSize>("RegisterWritesQueueSize",        INFINITE), 2),
    m_cleanup       (clock, config.getInteger<BufferSize>("ThreadCleanupQueueSize",         INFINITE), 4),
    m_allocations   (clock, config.getInteger<BufferSize>("FamilyAllocationQueueSize",      INFINITE)),
    m_allocationsEx (clock, config.getInteger<BufferSize>("FamilyAllocationExclusiveQueueSize", INFINITE)),
    m_createState   (CREATE_INITIAL),
    m_readyThreads1 (clock, threadTable),
    m_readyThreads2 (clock, threadTable),

    m_maxallocex(0), m_totalallocex(0), m_lastcycle(0), m_curallocex(0),

    p_ThreadAllocate  ("thread-allocate",   delegate::create<Allocator, &Allocator::DoThreadAllocate  >(*this) ),
    p_FamilyAllocate  ("family-allocate",   delegate::create<Allocator, &Allocator::DoFamilyAllocate  >(*this) ),
    p_FamilyCreate    ("family-create",     delegate::create<Allocator, &Allocator::DoFamilyCreate    >(*this) ),
    p_ThreadActivation("thread-activation", delegate::create<Allocator, &Allocator::DoThreadActivation>(*this) ),
    p_RegWrites       ("reg-write-queue",   delegate::create<Allocator, &Allocator::DoRegWrites       >(*this) ),
    
    p_allocation    (*this, clock, "p_allocation"),
    p_alloc         (*this, clock, "p_alloc"),
    p_readyThreads  (*this, clock, "p_readyThreads"),
    p_activeThreads (*this, clock, "p_activeThreads"),
    m_activeThreads (clock, threadTable)
{
    m_alloc         .Sensitive(p_ThreadAllocate);
    m_creates       .Sensitive(p_FamilyCreate);
    m_registerWrites.Sensitive(p_RegWrites);
    m_cleanup       .Sensitive(p_ThreadAllocate);
    m_allocations   .Sensitive(p_FamilyAllocate);
    m_allocationsEx .Sensitive(p_FamilyAllocate);
    m_readyThreads1 .Sensitive(p_ThreadActivation);
    m_readyThreads2 .Sensitive(p_ThreadActivation);
    m_activeThreads .Sensitive(pipeline.p_Pipeline); // Fetch Stage is sensitive on this list

    RegisterSampleVariableInObject(m_totalallocex, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_maxallocex, SVC_WATERMARK);
    RegisterSampleVariableInObject(m_curallocex, SVC_LEVEL);
}

void Allocator::AllocateInitialFamily(MemAddr pc, bool legacy)
{
    static const unsigned char InitialRegisters[NUM_REG_TYPES] = {31, 31};

    LFID fid = m_familyTable.AllocateFamily(CONTEXT_NORMAL);
    if (fid == INVALID_LFID)
    {
        throw SimulationException("Unable to create initial family", *this);
    }
    UpdateContextAvailability();

    // Set initial place to the whole group
    InitializeFamily(fid, PLACE_GROUP);
    
    Family& family = m_familyTable[fid];
    family.start         = 0;
    family.step          = 1;
    family.nThreads      = 1;
    family.virtBlockSize = 1;
    family.physBlockSize = 1;
    family.parent_lpid   = m_lpid;
    family.legacy        = legacy;
    family.pc            = pc;
    family.state         = FST_ACTIVE;

    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
        family.regs[i].count.locals  = InitialRegisters[i];
        family.regs[i].count.globals = 0;
        family.regs[i].count.shareds = 0;
    };

    // But we want a local family
    ReinitializeFamily(fid, Family::LOCAL);
    
    // The main family starts off detached
    family.dependencies.detached = true;
    family.dependencies.breaked  = false;

    if (!AllocateRegisters(fid, CONTEXT_NORMAL))
    {
        throw SimulationException("Unable to create initial family", *this);
    }
    
    m_threadTable.ReserveThread();

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
                    switch (p->place)
                    {
                    case PLACE_GROUP: out << "Group"; break;
                    case PLACE_LOCAL: out << "Local"; break;
                    default: assert(false); break;
                    }
                    out << "@R" << hex << uppercase << setw(4) << setfill('0') << p->reg << nouppercase << dec;
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
            for (Buffer<LFID>::const_iterator f = m_alloc.begin(); f != m_alloc.end();)
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
            for (Buffer<CreateInfo>::const_iterator p = m_creates.begin(); p != m_creates.end(); )
            {
                out << "F" << p->fid;
                if (p->completion != INVALID_REG_INDEX) {
                    out << " (R" << p->completion << ")";
                }
                if (++p != m_creates.end()) {
                    out << ", ";
                }
            }
            out << endl;
            out << "Create state for F" << m_creates.begin()->fid << ": ";
            switch (m_createState)
            {
                case CREATE_INITIAL:              out << "Initial"; break;
                case CREATE_LOADING_LINE:         out << "Loading cache-line"; break;
                case CREATE_LINE_LOADED:          out << "Cache-line loaded"; break;
                case CREATE_ALLOCATING_REGISTERS: out << "Allocating registers"; break;
                case CREATE_ACQUIRE_TOKEN:        out << "Acquire token"; break;
                case CREATE_ACQUIRING_TOKEN:      out << "Acquiring token"; break;
                case CREATE_BROADCASTING_CREATE:  out << "Broadcasting create"; break;
                case CREATE_ACTIVATING_FAMILY:    out << "Activating family"; break;
                case CREATE_NOTIFY:               out << "Notifying completion"; break;
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
