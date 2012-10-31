#include "Processor.h"
#include <sim/config.h>
#include <sim/sampling.h>
#include <arch/symtable.h>

#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

/// String representation for the AllocationType enumeration
static const char* const AllocationTypes[] = {
    "Normal", "Exact", "Balanced", "Single"
};

void Processor::Allocator::UpdateStats()
{
    CycleNo cycle = GetKernel()->GetCycleNo();
    CycleNo elapsed = cycle - m_lastcycle;
    m_lastcycle = cycle;

    m_curallocex = m_allocRequestsExclusive.size();

    m_totalallocex += m_curallocex * elapsed;
    m_maxallocex = std::max(m_maxallocex, m_curallocex);
}

RegAddr Processor::Allocator::GetRemoteRegisterAddress(LFID fid, RemoteRegType kind, const RegAddr& addr) const
{
    const Family&          family = m_familyTable[fid];
    const Family::RegInfo& regs   = family.regs[addr.type];

    assert(family.state != FST_EMPTY);

    RegIndex base;
    RegSize  size;
    switch (kind)
    {
        case RRT_GLOBAL:
            base = regs.base + regs.size - regs.count.globals;
            size = regs.count.globals;
            break;

        case RRT_LAST_SHARED:
            // Get the last allocated thread's shareds
            assert(regs.last_shareds != INVALID_REG_INDEX);
            base = regs.last_shareds;
            size = regs.count.shareds;
            break;

        case RRT_FIRST_DEPENDENT:
            // Return the dependent address for the first thread in the family
            // This is simply the base of the family's registers.
            // This is the first allocated thread's dependents.
            base = regs.base;
            size = regs.count.shareds;
            break;

        default:
            assert(false);
            return INVALID_REG;
    }
    return (addr.index < size) ? MAKE_REGADDR(addr.type, base + addr.index) : INVALID_REG;
}

// Administrative function for getting a register's type and thread mapping
TID Processor::Allocator::GetRegisterType(LFID fid, RegAddr addr, RegClass* group) const
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

bool Processor::Allocator::QueueActiveThreads(const ThreadQueue& threads)
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

TID Processor::Allocator::PopActiveThread()
{
    TID tid = INVALID_TID;
    if (!m_activeThreads.Empty())
    {
        tid = m_activeThreads.Front();
        m_activeThreads.Pop();
        COMMIT{--m_numThreadsPerState[TST_ACTIVE];}
    }
    return tid;
}

//
// Adds the list of threads to the family's active queue.
// There is assumed to be a linked list between the threads.head
// and threads.tail thread by Thread::nextState.
//
bool Processor::Allocator::QueueThreads(ThreadList& list, const ThreadQueue& threads, ThreadState state)
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
            next = m_threadTable[cur].next;
            m_threadTable[cur].state = state;
            ++count;

            DebugSimWrite("F%u/T%u -> %s", (unsigned)m_threadTable[cur].family, (unsigned)cur, ThreadStateNames[state]);

        } while (cur != threads.tail);
        m_numThreadsPerState[state] += count;
    }
    return true;
}

//
// This is called by various components (RegisterFile, Pipeline, ...) to
// add the threads to the ready queue.
//
bool Processor::Allocator::ActivateThreads(const ThreadQueue& threads)
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
bool Processor::Allocator::KillThread(TID tid)
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

    DebugSimWrite("F%u/T%u(%llu) terminated", (unsigned)thread.family, (unsigned)tid, (unsigned long long)thread.index);

    COMMIT
    {
        thread.cid    = INVALID_CID;
        thread.state  = TST_TERMINATED;
    }
    return true;
}

//
// Reschedules the thread at the specified PC.
// Called from the pipeline.
//
bool Processor::Allocator::RescheduleThread(TID tid, MemAddr pc)
{
    Thread& thread = m_threadTable[tid];
    assert(thread.state == TST_RUNNING);

    // Save the (possibly overriden) program counter
    COMMIT
    {
        thread.pc = pc;
        thread.next = INVALID_TID;
    }

    if (!m_icache.ReleaseCacheLine(thread.cid))
    {
        DeadlockWrite("F%u/T%u unable to release iline #%u", (unsigned)thread.family, (unsigned)tid, (unsigned)thread.cid);
        return false;
    }

    // The thread can be added to the ready queue
    ThreadQueue tq = {tid, tid};
    if (!ActivateThreads(tq))
    {
        DeadlockWrite("F%u/T%u unable to reschedule", (unsigned)thread.family, (unsigned)tid);
        return false;
    }

    DebugSimWrite("F%u/T%u(%llu) rescheduling to %s",
                  (unsigned)thread.family, (unsigned)tid, (unsigned long long)thread.index,
                  m_parent.GetSymbolTable()[pc].c_str());
    return true;
}

//
// Suspends the thread at the specific PC.
// Called from the pipeline.
//
bool Processor::Allocator::SuspendThread(TID tid, MemAddr pc)
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
bool Processor::Allocator::AllocateThread(LFID fid, TID tid, bool isNewlyAllocated)
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

    // We should still have threads to run, obviously
    assert(family->nThreads > 0);

    // Initialize thread
    thread->cid              = INVALID_CID;
    thread->pc               = family->pc;
    thread->family           = fid;
    thread->index            = family->start;   // Administrative field, useful for debugging
    thread->nextInBlock      = INVALID_TID;
    thread->waitingForWrites = false;
    thread->next             = INVALID_TID;

    // Initialize dependencies
    thread->dependencies.prevCleanedUp    = family->prevCleanedUp || !family->hasShareds || family->dependencies.numThreadsAllocated == 0 || family->physBlockSize == 1;
    thread->dependencies.killed           = false;
    thread->dependencies.numPendingWrites = 0;

    family->prevCleanedUp = false;

    if (family->lastAllocated != INVALID_TID)
    {
        COMMIT{ m_threadTable[family->lastAllocated].nextInBlock = tid; }
    }

    // Set the register information for the new thread
    for (size_t i = 0; i < NUM_REG_TYPES; i++)
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
    }

    COMMIT
    {
        // Reserve the memory (commits on use)
        const MemAddr tls_base = m_parent.GetTLSAddress(fid, tid);
        const MemSize tls_size = m_parent.GetTLSSize();
        m_parent.MapMemory(tls_base+tls_size/2, tls_size/2);
    }

    SInteger logical_index = family->start;

    // Write L0 to the register file
    if (family->regs[RT_INTEGER].count.locals > 0)
    {
        RegAddr  addr = MAKE_REGADDR(RT_INTEGER, thread->regs[RT_INTEGER].locals);
        RegValue data;

        if (!m_registerFile.p_asyncW.Write(addr))
        {
            DeadlockWrite("F%u/T%u(%llu) %s unable to acquire RF port to write",
                          (unsigned)fid, (unsigned)tid, (unsigned long long)logical_index,
                          addr.str().c_str());
            return false;
        }

        if (!m_registerFile.ReadRegister(addr, data))
        {
            DeadlockWrite("F%u/T%u(%llu) %s unable to read index register",
                          (unsigned)fid, (unsigned)tid, (unsigned long long)logical_index,
                          addr.str().c_str());
            return false;
        }

        assert(data.m_state != RST_WAITING);
        if (data.m_state == RST_PENDING)
        {
            DeadlockWrite("F%u/T%u(%llu) %s index register pending",
                          (unsigned)fid, (unsigned)tid, (unsigned long long)logical_index,
                          addr.str().c_str());
            return false;
        }

        data.m_state   = RST_FULL;
        data.m_integer = logical_index;

        if (!m_registerFile.WriteRegister(addr, data, false))
        {
            DeadlockWrite("F%u/T%u(%llu) %s unable to write index register",
                          (unsigned)fid, (unsigned)tid, (unsigned long long)logical_index,
                          addr.str().c_str());
            return false;
        }

        DebugSimWrite("F%u/T%u(%llu) %s wrote index register",
                      (unsigned)fid, (unsigned)tid, (unsigned long long)logical_index,
                      addr.str().c_str());
    }

    if (isNewlyAllocated)
    {
        assert(family->dependencies.numThreadsAllocated < family->physBlockSize);

        // Increase the allocation count
        family->dependencies.numThreadsAllocated++;
    }

    //
    // Update family information
    //
    if (family->hasShareds && family->physBlockSize > 1)
    {
        family->lastAllocated = tid;
    }

    family->start    += family->step;
    family->nThreads -= 1;

    if (family->nThreads == 0)
    {
        // We're done allocating threads
        if (!DecreaseFamilyDependency(fid, FAMDEP_ALLOCATION_DONE))
        {
            DeadlockWrite("F%u unable to mark ALLOCATION_DONE", (unsigned)fid);
            return FAILED;
        }
    }

    ThreadQueue tq = {tid, tid};
    if (!ActivateThreads(tq))
    {
        // Abort allocation
        DeadlockWrite("F%u/T%u(%llu) unable to activate",
                      (unsigned)fid, (unsigned)tid, (unsigned long long)logical_index);
        return false;
    }

    // Statistics
    COMMIT{
        ++m_numCreatedThreads;
    }

    DebugSimWrite("F%u/T%u(%llu) created",
                  (unsigned)fid, (unsigned)tid, (unsigned long long)logical_index);
    return true;
}

bool Processor::Allocator::DecreaseFamilyDependency(LFID fid, FamilyDependency dep)
{
    return DecreaseFamilyDependency(fid, m_familyTable[fid], dep);
}

bool Processor::Allocator::DecreaseFamilyDependency(LFID fid, Family& family, FamilyDependency dep)
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
    case FAMDEP_SYNC_SENT:         assert(!deps->syncSent);               deps->syncSent         = true; break;
    case FAMDEP_DETACHED:          assert(!deps->detached);               deps->detached         = true; break;
    }

    switch (dep)
    {
    case FAMDEP_THREAD_COUNT:
    case FAMDEP_ALLOCATION_DONE:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone)
        {
            COMMIT{ family.state = FST_TERMINATED; }
            DebugSimWrite("F%u terminated", (unsigned)fid);
        }
        // Fall through

    case FAMDEP_PREV_SYNCHRONIZED:
    case FAMDEP_OUTSTANDING_READS:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone &&
            deps->numPendingReads     == 0 && deps->prevSynchronized)
        {
            // Forward synchronization token
            COMMIT{ family.sync.done = true; }

            if (family.link != INVALID_LFID)
            {
                // Send family termination event to next processor
                LinkMessage msg;
                msg.type        = LinkMessage::MSG_DONE;
                msg.done.fid    = family.link;
                msg.done.broken = family.broken;

                if (!m_network.SendMessage(msg))
                {
                    DeadlockWrite("F%u unable to buffer termination to next processor",
                                  (unsigned)fid);
                    return false;
                }
                DebugSimWrite("F%u forwarded synchronization token", (unsigned)fid);
            }
            // This is the last core of the family. All other cores have
            // finished. Write back the completion.
            else if (family.sync.pid != INVALID_PID)
            {
                // A thread is synching on this family
                Network::SyncInfo info;
                info.fid = fid;
                info.pid = family.sync.pid;
                info.reg = family.sync.reg;
                info.broken = family.broken;

                if (!m_network.SendSync(info))
                {
                    DeadlockWrite("F%u unable to buffer remote sync writeback %d to CPU%u/R%04x",
                                  (unsigned)fid, (unsigned)info.pid, (int)info.broken, (unsigned)info.reg);
                    return false;
                }
                DebugSimWrite("F%u buffered termination writeback %d for CPU%u/R%04x",
                              (unsigned)fid, (unsigned)info.pid, (int)info.broken, (unsigned)info.reg);
            }

            DebugSimWrite("F%u synchronized", (unsigned)fid);
        }
        // Fall through

    case FAMDEP_SYNC_SENT:
    case FAMDEP_DETACHED:
        if (deps->numThreadsAllocated == 0 && deps->allocationDone &&
            deps->numPendingReads     == 0 && deps->prevSynchronized &&
            deps->detached                 && deps->syncSent)
        {
            ContextType context = m_familyTable.IsExclusive(fid) ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL;

            // Release registers
            RegIndex indices[NUM_REG_TYPES];
            for (size_t i = 0; i < NUM_REG_TYPES; i++)
            {
                indices[i] = family.regs[i].base;
            }
            m_raunit.Free(indices, context);

            // Free the family table entry
            m_familyTable.FreeFamily(fid, context);

            DebugSimWrite("F%u cleaned up", (unsigned)fid);
        }
        break;
    }

    return true;
}

bool Processor::Allocator::OnMemoryRead(LFID fid)
{
    COMMIT{ m_familyTable[fid].dependencies.numPendingReads++; }
    return true;
}

bool Processor::Allocator::DecreaseThreadDependency(TID tid, ThreadDependency dep)
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

bool Processor::Allocator::IncreaseThreadDependency(TID tid, ThreadDependency dep)
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

Processor::Family& Processor::Allocator::GetFamilyChecked(LFID fid, FCapability capability) const
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

// Initializes the family entry with default values.
FCapability Processor::Allocator::InitializeFamily(LFID fid) const
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
        //family.virtBlockSize = 0;
        family.physBlockSize = 0;
        family.link          = INVALID_LFID;
        family.sync.done     = false;
        family.sync.pid      = INVALID_PID;
        family.hasShareds    = false;
        family.lastAllocated = INVALID_TID;
        family.prevCleanedUp = false;
        family.broken        = false;

        // Dependencies
        family.dependencies.allocationDone      = false;
        family.dependencies.numPendingReads     = 0;
        family.dependencies.numThreadsAllocated = 0;
        family.dependencies.detached            = false;
        family.dependencies.syncSent            = true;
    }

    return capability;
}

bool Processor::Allocator::IsContextAvailable(ContextType type) const
 {
    return m_raunit     .GetNumFreeContexts(type) > 0 &&
           m_threadTable.GetNumFreeThreads(type)  > 0 &&
           m_familyTable.GetNumFreeFamilies(type) > 0;
}

bool Processor::Allocator::ActivateFamily(LFID fid)
{
    if (!p_alloc.Invoke())
    {
        return false;
    }

    // Update the family state
    COMMIT
    {
        Family& family = m_familyTable[fid];
        family.state = FST_ACTIVE;

        // Statistics
        ++m_numCreatedFamilies;
    }

    m_alloc.Push(fid);
    return true;
}

bool Processor::Allocator::AllocateRegisters(LFID fid, ContextType type)
{
    // Try to allocate registers
    Family& family = m_familyTable[fid];

    for (FSize physBlockSize = std::max<TSize>(1, family.physBlockSize); physBlockSize > 0; physBlockSize--)
    {
        // Calculate register requirements
        RegSize sizes[NUM_REG_TYPES];
        for (size_t i = 0; i < NUM_REG_TYPES; i++)
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
            std::stringstream str;
            for (size_t i = 0; i < NUM_REG_TYPES; i++)
            {
                RegIndex base = INVALID_REG_INDEX;
                if (sizes[i] > 0)
                {
                    // Clear the allocated registers
                    m_registerFile.Clear(MAKE_REGADDR((RegType)i, indices[i]), sizes[i]);
                    base = indices[i];
                }

                COMMIT
                {
                    Family::RegInfo& regs = family.regs[i];
                    regs.base             = base;
                    regs.size             = sizes[i];
                    regs.last_shareds     = base;

                    static const char* RegTypeNames[] = {" int", " flt"};
                    str << dec << sizes[i] << RegTypeNames[i] << " regs";
                    if (sizes[i] > 0) {
                        RegAddr addr = MAKE_REGADDR((RegType)i, indices[i]);
                        str << " at " << addr.str();
                    }

                    if (i < NUM_REG_TYPES - 1) {
                        str << ", ";
                    }
                }
            }

            DebugSimWrite("F%u allocated %s (physical block size adjusted %u -> %u)",
                          (unsigned)fid,
                          str.str().c_str(),
                          (unsigned)family.physBlockSize,
                          (unsigned)physBlockSize);

            COMMIT{ family.physBlockSize = physBlockSize; }
            return true;
        }
    }

    return false;
}

bool Processor::Allocator::OnICachelineLoaded(CID cid)
{
    assert(!m_creates.Empty());
    COMMIT{
        m_createState = CREATE_LINE_LOADED;
        m_createLine  = cid;
    }
    return true;
}

bool Processor::Allocator::OnDCachelineLoaded(char* data)
{
    assert(!m_bundle.Empty());
    COMMIT {
        m_bundleState = BUNDLE_LINE_LOADED;
        memcpy(m_bundleData, data, sizeof(m_bundleData));
    }
    return true;
}

Result Processor::Allocator::DoThreadAllocate()
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

        m_cleanup.Pop();

        assert(thread.state == TST_TERMINATED);

        // Clear the thread's dependents, if any
        for (size_t i = 0; i < NUM_REG_TYPES; i++)
        {
            if (family.regs[i].count.shareds > 0)
            {
                if (!m_registerFile.Clear(MAKE_REGADDR((RegType)i, thread.regs[i].dependents), family.regs[i].count.shareds))
                {
                    DeadlockWrite("F%u/T%u(%llu) unable to clear the dependent registers",
                                  (unsigned)fid, (unsigned)tid, (unsigned long long)thread.index);
                    return FAILED;
                }
            }
        }

        if (family.hasShareds && family.physBlockSize > 1)
        {
            // Mark 'previous thread cleaned up' on the next thread
            if (thread.nextInBlock == INVALID_TID)
            {
                COMMIT{ family.prevCleanedUp = true; }
                DebugSimWrite("F%u/T%u(%llu) marking PREV_CLEANED_UP on family (no next thread)",
                              (unsigned)fid, (unsigned)tid, (unsigned long long)thread.index);
            }
            else if (!DecreaseThreadDependency(thread.nextInBlock, THREADDEP_PREV_CLEANED_UP))
            {
                DeadlockWrite("F%u/T%u(%llu) marking PREV_CLEANED_UP on next T%u",
                              (unsigned)fid, (unsigned)tid, (unsigned long long)thread.index,
                              (unsigned)thread.nextInBlock);
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

        if (family.dependencies.allocationDone)
        {
            // Release the thread.
            // We release the last thread in an exclusive family as an exclusive thread.
            ContextType context = (m_familyTable.IsExclusive(fid) && family.dependencies.numThreadsAllocated == 1) ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL;
            m_threadTable.PushEmpty(tid, context);

            if (!DecreaseFamilyDependency(fid, FAMDEP_THREAD_COUNT))
            {
                DeadlockWrite("F%u/T%u(%llu) unable to decrease thread count in family during thread cleanup",
                              (unsigned)fid, (unsigned)tid, (unsigned long long)thread.index);
                return FAILED;
            }

            DebugSimWrite("F%u/T%u(%llu) cleaned up",
                          (unsigned)fid, (unsigned)tid, (unsigned long long)thread.index);
        }
        // Reallocate thread
        else if (!AllocateThread(fid, tid, false))
        {
            DeadlockWrite("F%u/T%u unable to reactivate",
                          (unsigned)fid, (unsigned)tid);
            return FAILED;
        }
        return SUCCESS;
    }

    assert (!m_alloc.Empty());
    {
        // Allocate an initial thread of a family
        LFID    fid    = m_alloc.Front();
        Family& family = m_familyTable[fid];

        // Check if we're done with the initial allocation of this family
        if (family.dependencies.numThreadsAllocated == family.physBlockSize || family.dependencies.allocationDone)
        {
            // We're done with this family
            DebugSimWrite("F%u done allocating threads", (unsigned)fid);
            m_alloc.Pop();
        }
        else if (family.nThreads == 0)
        {
            // We're done allocating/creating threads
            if (!DecreaseFamilyDependency(fid, FAMDEP_ALLOCATION_DONE))
            {
                DeadlockWrite("F%u unable to mark ALLOCATION_DONE", (unsigned)fid);
                return FAILED;
            }
        }
        else
        {
            // We only allocate from a special pool once:
            // for the first thread of the family.
            bool exclusive = family.dependencies.numThreadsAllocated == 0 && m_familyTable.IsExclusive(fid);
            bool reserved  = family.dependencies.numThreadsAllocated == 0;

            // We have threads to run
            TID tid = m_threadTable.PopEmpty( exclusive ? CONTEXT_EXCLUSIVE : (reserved ? CONTEXT_RESERVED : CONTEXT_NORMAL) );
            if (tid == INVALID_TID)
            {
                assert(!exclusive && !reserved);
                DeadlockWrite("F%u unable to allocate a free thread entry", (unsigned)fid);
                return FAILED;
            }

            if (!AllocateThread(fid, tid, true))
            {
                DeadlockWrite("F%u/T%u unable to activate", (unsigned)fid, (unsigned)tid);
                return FAILED;
            }
        }
        return SUCCESS;
    }
}


bool Processor::Allocator::QueueBundle(const MemAddr addr, Integer parameter, RegIndex completion_reg)
{
    BundleInfo info;
    info.addr           = addr;
    info.parameter      = parameter;
    info.completion_reg = completion_reg;

    if (!m_bundle.Push(info))
    {
        DeadlockWrite("unable to queue bundle 0x%016llx, %llu, R%04x",
                      (unsigned long long)addr, (unsigned long long)parameter, (unsigned)completion_reg);
        return false;
    }

    DebugSimWrite("queued bundle 0x%016llx, %llu, R%04x",
                  (unsigned long long)addr, (unsigned long long)parameter, (unsigned)completion_reg);
    return true;
}


/// Queues an allocation request for a family entry and context
bool Processor::Allocator::QueueFamilyAllocation(const RemoteMessage& msg, bool bundle)
{
    // Can't be balanced; that should have been handled by
    // the network before it gets here.
    assert(msg.allocate.type != ALLOCATE_BALANCED);

    // Can't be exclusive and non-single.
    assert(!msg.allocate.exclusive || msg.allocate.type == ALLOCATE_SINGLE);

    // Place the request in the appropriate buffer
    AllocRequest request;
    request.first_fid      = INVALID_LFID;
    request.prev_fid       = INVALID_LFID;
    request.placeSize      = msg.allocate.place.size;
    request.type           = msg.allocate.type;
    request.completion_reg = msg.allocate.completion_reg;
    request.completion_pid = msg.allocate.completion_pid;
    request.bundle         = bundle;

    if (bundle)
    {
        request.binfo.pc         = msg.allocate.bundle.pc;
        request.binfo.parameter  = msg.allocate.bundle.parameter;
        request.binfo.index      = msg.allocate.bundle.index;
    }
    else
    {
        request.binfo.pc         = 0;
        request.binfo.parameter  = 0;
        request.binfo.index      = 0;
    }

    Buffer<AllocRequest>& allocations = msg.allocate.exclusive
        ? m_allocRequestsExclusive
        : (msg.allocate.suspend ? m_allocRequestsSuspend : m_allocRequestsNoSuspend);

    if (!allocations.Push(request))
    {
        return false;
    }

    DebugSimWrite("accepted incoming allocation for %u cores (%s) for CPU%u/R%04x",
                  (unsigned)msg.allocate.place.size, AllocationTypes[msg.allocate.type],
                  (unsigned)request.completion_pid, (unsigned)request.completion_reg);
    return true;
}

/// Queues an allocation request for a family entry and context
bool Processor::Allocator::QueueFamilyAllocation(const LinkMessage& msg)
{
    // Place the request in the appropriate buffer
    AllocRequest request;
    request.first_fid       = msg.allocate.first_fid;
    request.prev_fid        = msg.allocate.prev_fid;
    request.placeSize       = msg.allocate.size;
    request.type            = msg.allocate.exact ? ALLOCATE_EXACT : ALLOCATE_NORMAL;
    request.completion_reg  = msg.allocate.completion_reg;
    request.completion_pid  = msg.allocate.completion_pid;
    request.bundle          = false;
    request.binfo.pc        = 0;
    request.binfo.parameter = 0;
    request.binfo.index     = 0;

    Buffer<AllocRequest>& allocations = (msg.allocate.suspend ? m_allocRequestsSuspend : m_allocRequestsNoSuspend);
    if (!allocations.Push(request))
    {
        return false;
    }

    DebugSimWrite("accepted link allocation for %u cores (exact: %s) for CPU%u/R%04x first CPU%u/F%u prev CPU%u/F%u",
                  (unsigned)msg.allocate.size, msg.allocate.exact ? "yes" : "no",
                  (unsigned)request.completion_pid, (unsigned)request.completion_reg,
                  (unsigned)(m_parent.GetPID() & ~(request.placeSize-1)), (unsigned)request.first_fid,
                  (unsigned)(m_parent.GetPID() - 1), (unsigned)request.prev_fid);
    return true;
}

void Processor::Allocator::ReleaseContext(LFID fid)
{
    m_familyTable.FreeFamily(fid, CONTEXT_NORMAL);
    m_raunit.UnreserveContext();
    m_threadTable.UnreserveThread();
}

LFID Processor::Allocator::AllocateContext(ContextType type, LFID prev_fid, PSize placeSize)
{
    if (!IsContextAvailable(type))
    {
        // No context available
        return INVALID_LFID;
    }

    // Grab a normal context
    LFID lfid = m_familyTable.AllocateFamily(type);
    assert(lfid != INVALID_LFID);

    if (type != CONTEXT_EXCLUSIVE)
    {
        assert(type == CONTEXT_NORMAL);
        m_raunit.ReserveContext();
        m_threadTable.ReserveThread();
    }

    InitializeFamily(lfid);

    COMMIT
    {
        // Set up some essential family table fields
        Family& family = m_familyTable[lfid];
        family.placeSize = placeSize;
        family.link      = prev_fid;

        // First core? Already synched.
        family.dependencies.prevSynchronized = (prev_fid == INVALID_LFID);
    }

    return lfid;
}

Result Processor::Allocator::DoFamilyAllocate()
{
    // Pick an allocation queue to allocate from:
    // If we can do an exclusive allocate, we do those first.
    // Otherwise, we prioritize non-suspending requests over suspending ones,
    // because we can always handle non-suspending requests.
    Buffer<AllocRequest>* buffer = NULL;

    if (!m_familyTable.IsExclusiveUsed() && !m_allocRequestsExclusive.Empty())
    {
        buffer = &m_allocRequestsExclusive;
    }
    else if (!m_allocRequestsNoSuspend.Empty())
    {
        buffer = &m_allocRequestsNoSuspend;
    }
    else if (!m_allocRequestsSuspend.Empty())
    {
        buffer = &m_allocRequestsSuspend;
    }

    if (buffer == NULL)
    {
        // remaining situation:
        // - there are no non-exclusive allocation requests ready to be handled;
        // - there is at least one exclusive allocation request waiting;
        // - the exclusive context is busy.
        DeadlockWrite("Exclusive context busy; exclusive allocation delayed.");
        return FAILED;
    }

    if (!p_allocation.Invoke())
    {
        DeadlockWrite("Unable to acquire service for family allocation");
        return FAILED;
    }

    const AllocRequest& req = buffer->Front();

    const ContextType type = (buffer == &m_allocRequestsExclusive) ? CONTEXT_EXCLUSIVE : CONTEXT_NORMAL;

    const LFID lfid = AllocateContext(type, req.prev_fid, req.placeSize);
    if ((lfid == INVALID_LFID) && (buffer != &m_allocRequestsNoSuspend))
    {
        // No family entry was available; stall
        DeadlockWrite("Unable to allocate a free context");
        return FAILED;
    }
    if (lfid == INVALID_LFID)
    {
        // Bundle creations are always suspending, so it is not
        // possible that the request is a bundle, allocation failed
        // and that the control flow arrives here.
        assert(!req.bundle);
        assert(req.completion_reg != INVALID_REG_INDEX);

        // No family entry was available and we don't want to suspend until one is.
        if (req.prev_fid == INVALID_LFID)
        {
            // Return 0 as FID to indicate failure.
            FID fid;
            fid.pid        = 0;
            fid.lfid       = 0;
            fid.capability = 0;

            RemoteMessage msg;
            msg.type                   = RemoteMessage::MSG_RAW_REGISTER;
            msg.rawreg.pid             = req.completion_pid;
            msg.rawreg.addr            = MAKE_REGADDR(RT_INTEGER, req.completion_reg);
            msg.rawreg.value.m_state   = RST_FULL;
            msg.rawreg.value.m_integer = m_parent.PackFID(fid);

            if (!m_network.SendMessage(msg))
            {
                DeadlockWrite("Unable to send remote allocation writeback");
                return FAILED;
            }

        }
        else
        {
            // Unwind
            AllocResponse ret;
            ret.numCores       = 0;
            ret.exact          = (req.type == ALLOCATE_EXACT);
            ret.prev_fid       = req.prev_fid;
            ret.next_fid       = INVALID_LFID;
            ret.completion_pid = req.completion_pid;
            ret.completion_reg = req.completion_reg;
            if (!m_network.SendAllocResponse(ret))
            {
                return FAILED;
            }
            DebugSimWrite("unwinding allocation for %u cores from CPU%u/R%04x prev F%u",
                          (unsigned)req.placeSize,
                          (unsigned)ret.completion_pid, (unsigned)ret.completion_reg,
                          (unsigned)ret.prev_fid);
        }
    }
    // Allocation succeeded
    else if (req.type == ALLOCATE_SINGLE || (m_parent.GetPID() + 1) % req.placeSize == 0)
    {
        // We've grabbed the last context that we wanted in the place
        Family& family = m_familyTable[lfid];
        COMMIT
        {
            family.numCores = (req.type == ALLOCATE_SINGLE) ? 1 : req.placeSize;
            family.link     = INVALID_LFID;
        }
        DebugSimWrite("F%u finished allocation on %u cores", (unsigned)lfid, (unsigned)family.numCores);
        
        if (!req.bundle)
        {
            // Here we have a regular allocation request, where either:
            // - we have reached the first core after committing, and the parent
            //   is waiting on an acknowledgement for the allocate itself. Send it.
            // - or we need to commit the request to the previous cores in the
            //   place via the link network.
            
            if (req.prev_fid == INVALID_LFID) 
            {
                // We're the only core in the family
                // Construct a global FID for this family
                FID fid;
                fid.pid        = m_parent.GetPID();
                fid.lfid       = lfid;
                fid.capability = family.capability;

                RemoteMessage msg;
                msg.type                   = RemoteMessage::MSG_RAW_REGISTER;
                msg.rawreg.pid             = req.completion_pid;
                msg.rawreg.addr            = MAKE_REGADDR(RT_INTEGER, req.completion_reg);
                msg.rawreg.value.m_state   = RST_FULL;
                msg.rawreg.value.m_integer = m_parent.PackFID(fid);

                if (!m_network.SendMessage(msg))
                {
                    DeadlockWrite("Unable to send remote allocation writeback");
                    return FAILED;
                }
                DebugSimWrite("F%u sent allocation response to CPU%u/R%04x",
                              (unsigned)lfid,
                              (unsigned)req.completion_pid, (unsigned)req.completion_reg);
            }
            else
            {
                // Commit the allocations for this place
                AllocResponse ret;
                ret.numCores       = req.placeSize;
                ret.exact          = (req.type == ALLOCATE_EXACT);
                ret.prev_fid       = req.prev_fid;
                ret.next_fid       = lfid;
                ret.completion_pid = req.completion_pid;
                ret.completion_reg = req.completion_reg;
                if (!m_network.SendAllocResponse(ret))
                {
                    DeadlockWrite("Unable to send allocation commit");
                    return FAILED;
                }
                DebugSimWrite("F%u backward link allocation response (writeback CPU%u/R%04x prev CPU%u/F%u)",
                              (unsigned)lfid,
                              (unsigned)ret.completion_pid, (unsigned)ret.completion_reg,
                              (unsigned)(m_parent.GetPID() - 1), (unsigned)ret.prev_fid);
            }
        }
        else
        {
            // bundle request:
            // do not notify the allocation; we need to wait for creation
            // before notifying. This is because otherwise the parent could issue
            // a sync before the creation occurs, and this is not supported. The
            // parent must wait until creation starts.

            // Instead, trigger creation by sending a creation request via loopback.
            
            RemoteMessage msg;
            msg.type                  = RemoteMessage::MSG_CREATE;
            msg.create.fid.pid        = m_parent.GetPID();
            msg.create.fid.lfid       = lfid;
            msg.create.fid.capability = family.capability;
            msg.create.address        = req.binfo.pc;
            msg.create.completion_reg = req.completion_reg;
            msg.create.completion_pid = req.completion_pid;
            msg.create.bundle         = true;
            msg.create.parameter      = req.binfo.parameter;
            msg.create.index          = req.binfo.index;

            if (!m_network.SendMessage(msg))
            {
                DeadlockWrite("Unable to send bundle creation to loopback");
                return FAILED;
            }

        }
    }
    else
    {
        // We need to allocate contexts on the other cores in the place
        // before we can write back the FID.
        LinkMessage msg;
        msg.type                    = LinkMessage::MSG_ALLOCATE;
        msg.allocate.first_fid      = lfid;
        msg.allocate.prev_fid       = lfid;
        msg.allocate.size           = req.placeSize;
        msg.allocate.exact          = (req.type == ALLOCATE_EXACT);
        msg.allocate.suspend        = (req.type == ALLOCATE_EXACT) && (buffer != &m_allocRequestsNoSuspend);
        msg.allocate.completion_pid = req.completion_pid;
        msg.allocate.completion_reg = req.completion_reg;


        if (!m_network.SendMessage(msg))
        {
            DeadlockWrite("Unable to forward family allocation requests");
            return FAILED;
        }
        // Already notified in SendMessage:
        //DebugSimWrite("forwarding link allocation for %u cores (exact: %s) from CPU%u/R%04x local F%u",
        //              (unsigned)req.placeSize, msg.allocate.exact ? "yes" : "no",
        //              (unsigned)req.completion_pid, (unsigned)req.completion_reg,
        //              (unsigned)lfid);
    }

    UpdateStats();
    buffer->Pop();
    return SUCCESS;
}


//For group create
bool Processor::Allocator::QueueCreate(const LinkMessage& msg)
{
    assert(msg.type == LinkMessage::MSG_CREATE);
    assert(msg.create.fid != INVALID_LFID);

    Family& family = m_familyTable[msg.create.fid];

    // Set information and lock family
    COMMIT
    {
        family.pc    = msg.create.address;  // Already aligned
        family.state = FST_CREATE_QUEUED;
    }

    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        family.regs[i].base  = INVALID_REG_INDEX;
        family.regs[i].count = msg.create.regs[i];
    }

    Integer nThreads = CalculateThreadCount(family.start, family.limit, family.step);
    CalculateDistribution(family, nThreads, family.numCores);

    DebugSimWrite("F%u (%llu threads, place CPU%u/%u) accepted link create %s start index %llu",
                  (unsigned)msg.create.fid, (unsigned long long)family.nThreads,
                  (unsigned)(m_parent.GetPID() & ~family.numCores),
                  (unsigned)family.numCores,
                  m_parent.GetSymbolTable()[msg.create.address].c_str(),
                  (unsigned long long)family.start);

    if (!AllocateRegisters(msg.create.fid, CONTEXT_RESERVED))
    {
        DeadlockWrite("F%u unable to allocate registers", (unsigned)msg.create.fid);
        return false;
    }

    if (!ActivateFamily(msg.create.fid))
    {
        DeadlockWrite("F%u unable to activate", (unsigned)msg.create.fid);
        return false;
    }

    if (family.link != INVALID_LFID)
    {
        // Forward the message
        LinkMessage fwd(msg);
        fwd.create.fid      = family.link;
        fwd.create.numCores = (msg.create.numCores > 0) ? msg.create.numCores - 1 : 0;
        if (!m_network.SendMessage(fwd))
        {
            return false;
        }

        if (fwd.create.numCores == 0)
        {
            // Last core in the restricted chain, clear the link.
            // Everything after this core will be cleaned up.
            COMMIT{ family.link = INVALID_LFID; }
        }
        DebugSimWrite("F%u forwarding create", (unsigned)msg.create.fid);
    }
    return true;
}

// For delegate/local create
bool Processor::Allocator::QueueCreate(const RemoteMessage& msg, PID src)
{
    assert(src                != INVALID_PID);
    assert(msg.create.fid.pid == m_parent.GetPID());

    Family& family = GetFamilyChecked(msg.create.fid.lfid, msg.create.fid.capability);

    // Set PC and lock family
    COMMIT
    {
        // Skip control and reg count words
        family.pc    = (msg.create.address & -(MemAddr)m_icache.GetLineSize()) + 2 * sizeof(Instruction);
        family.state = FST_CREATE_QUEUED;
    }

    // Queue the create
    CreateInfo info;
    info.fid            = msg.create.fid.lfid;
    info.completion_pid = src;
    info.completion_reg = msg.create.completion_reg;
    info.bundle         = msg.create.bundle;

    if (info.bundle)
    {
        info.parameter      = msg.create.parameter;
        info.index          = msg.create.index;
    }
    else
    {
        info.parameter      = 0;
        info.index          = 0;
    }


    if (!m_creates.Push(info))
    {
        DeadlockWrite("Unable to queue create");
        return false;
    }

    DebugSimWrite("F%u queued create %s from CPU%u/R%04x",
                  (unsigned)info.fid,
                  m_parent.GetSymbolTable()[msg.create.address].c_str(),
                  (unsigned)info.completion_pid, (unsigned)info.completion_reg);
    return true;
}


Result Processor::Allocator::DoBundle()
{
    // handle system call
    assert(!m_bundle.Empty());
    const BundleInfo& info = m_bundle.Front();
    if (m_bundleState == BUNDLE_INITIAL)
    {
        Result      result;
        if ((result = m_dcache.Read(info.addr, m_bundleData, sizeof(Integer) * 2 + sizeof(MemAddr), 0)) == FAILED)
        {
            DeadlockWrite("Unable to fetch the D-Cache line for %#016llx for bundle creation", (unsigned long long)info.addr);
            return FAILED;
        }

        COMMIT
        {
            if (result == SUCCESS)
            {
            // Cache hit, proceed to loaded stage
                m_bundleState = BUNDLE_LINE_LOADED;

            }
            else
            {
            // Cache miss, line is being fetched.
            // The D-Cache will notify us with onDCachelineLoaded().
                m_bundleState = BUNDLE_LOADING_LINE;
            }
        }


    }
    else if (m_bundleState == BUNDLE_LOADING_LINE)
    {
        DeadlockWrite("Waiting for the Dcache-line to be loaded");
        return FAILED;
    }
    else if (m_bundleState == BUNDLE_LINE_LOADED)
    {
        RemoteMessage msg;
        msg.type                       = RemoteMessage::MSG_BUNDLE;

        msg.allocate.place             = m_parent.UnpackPlace(UnserializeRegister(RT_INTEGER,&m_bundleData[0], sizeof(Integer)));

        if (msg.allocate.place.size == 0)
        {
            throw exceptf<SimulationException>("Invalid place size in bundle creation");
        }

        msg.allocate.completion_reg    = info.completion_reg;
        msg.allocate.completion_pid    = m_parent.GetPID();
        msg.allocate.type              = ALLOCATE_SINGLE;
        msg.allocate.suspend           = true;
        msg.allocate.exclusive         = true;
        msg.allocate.bundle.pc         = UnserializeRegister(RT_INTEGER, &m_bundleData[sizeof(Integer) ], sizeof(MemAddr));
        msg.allocate.bundle.parameter  = info.parameter;
        msg.allocate.bundle.index      = UnserializeRegister(RT_INTEGER, &m_bundleData[sizeof(Integer) + sizeof(MemAddr)], sizeof(SInteger));

        DebugSimWrite("Processing bundle creation for CPU%u/%u, PC %#016llx, parameter %#016llx, index %#016llx",
                      (unsigned)msg.allocate.place.pid, (unsigned)msg.allocate.place.size,
                      (unsigned long long)msg.allocate.bundle.pc,
                      (unsigned long long)msg.allocate.bundle.parameter,
                      (unsigned long long)msg.allocate.bundle.index);

        if (!m_network.SendMessage(msg))
        {
            DeadlockWrite("Unable to send indirect creation to CPU%u", (unsigned)msg.allocate.place.pid);
            return FAILED;
        }


        // Reset the indirect create state
        COMMIT{ m_bundleState = BUNDLE_INITIAL; }
        m_bundle.Pop();
    }

    return SUCCESS;



}

Result Processor::Allocator::DoFamilyCreate()
{
    // The create at the front of the queue is the current create
    assert(!m_creates.Empty());
    const CreateInfo& info = m_creates.Front();

    if (m_createState == CREATE_INITIAL)
    {
        // Based on the indices, calculate the number of threads in the family
        // This is needed for the next step, where we calculate the number of cores
        // actually required.
        Family& family = m_familyTable[info.fid];

        // with bundle creations, the range is 1 thread starting at the
        // specified index value.
        Integer nThreads;
        if (info.bundle)
        {
            COMMIT {
                family.start = info.index;
                family.limit = info.index + 1;
            }
            nThreads = 1;
        }
        else
        {
            nThreads = CalculateThreadCount(family.start, family.limit, family.step);
        }

        COMMIT{
            family.nThreads = nThreads;

            family.state = FST_CREATING;
        }

        if (nThreads == 0)
        {
            DebugSimWrite("F%u is empty, skipping loading program header", (unsigned)info.fid);

            COMMIT { m_createState = CREATE_LINE_LOADED; }
        }
        else
        {
            COMMIT { m_createState = CREATE_LOAD_REGSPEC; }
        }

    }
    else if (m_createState == CREATE_LOAD_REGSPEC)
    {
        Family& family = m_familyTable[info.fid];

        DebugSimWrite("F%u start creation %s",
                      (unsigned)info.fid, m_parent.GetSymbolTable()[family.pc].c_str());

        // Load the register counts from the family's first cache line
        Instruction counts;
        CID         cid;
        Result      result;
        if ((result = m_icache.Fetch(family.pc - sizeof(Instruction), sizeof(counts), cid)) == FAILED)
        {
            DeadlockWrite("Unable to fetch the I-Cache line for 0x%016llx for F%u", (unsigned long long)family.pc, (unsigned)info.fid);
            return FAILED;
        }

        COMMIT
        {
            if (result == SUCCESS)
            {
                // Cache hit, proceed to loaded stage
                m_createState = CREATE_LINE_LOADED;
                m_createLine  = cid;
            }
            else
            {
                // Cache miss, line is being fetched.
                // The I-Cache will notify us with onCachelineLoaded().
                m_createState = CREATE_LOADING_LINE;
            }
        }
    }
    else if (m_createState == CREATE_LOADING_LINE)
    {
        DeadlockWrite("Waiting for the cache-line to be loaded");
        return FAILED;
    }
    else if (m_createState == CREATE_LINE_LOADED)
    {
        Family& family = m_familyTable[info.fid];

        // Read the register counts from the cache-line
        Instruction counts;

        if (family.nThreads > 0)
        {
            if (!m_icache.Read(m_createLine, family.pc - sizeof(Instruction), &counts, sizeof(counts)))
            {
                DeadlockWrite("Unable to read the I-Cache line for 0x%016llx for F%u", (unsigned long long)family.pc, (unsigned)info.fid);
                return FAILED;
            }
            // Release the cache-lined held by the create so far
            if (!m_icache.ReleaseCacheLine(m_createLine))
            {
                DeadlockWrite("Unable to release cache line for F%u", (unsigned)info.fid);
                return FAILED;
            }
            counts = UnserializeInstruction(&counts);
        }
        else
        {
            // Empty family, reduce all counts to 0.
            counts = 0;
        }

        COMMIT
        {
            RegsNo regcounts[NUM_REG_TYPES];
            bool   hasShareds = false;
            for (size_t i = 0; i < NUM_REG_TYPES; i++)
            {
                Instruction c = counts >> (i * 16);
                regcounts[i].globals = (unsigned char)((c >>  0) & 0x1F);
                regcounts[i].shareds = (unsigned char)((c >>  5) & 0x1F);
                regcounts[i].locals  = (unsigned char)((c >> 10) & 0x1F);
                if (regcounts[i].shareds > 0)
                {
                    hasShareds = true;
                }

                if (regcounts[i].globals + 2 * regcounts[i].shareds + regcounts[i].locals > 31)
                {
                    DebugProgWrite("Invalid register counts for type %d: %d %d %d\n", (int)i,
                                   (int)regcounts[i].globals, (int)regcounts[i].shareds, (int)regcounts[i].locals);
                    throw InvalidArgumentException(*this, "Too many registers specified in thread body");
                }
            }

            for (size_t i = 0; i < NUM_REG_TYPES; i++)
            {
                family.regs[i].base  = INVALID_REG_INDEX;
                family.regs[i].count = regcounts[i];
            }

            family.hasShareds = hasShareds;
        }

        DebugSimWrite("F%u (%llu threads) register counts loaded", (unsigned)info.fid, (unsigned long long)family.nThreads);

        // Advance to next stage
        COMMIT{ m_createState = CREATE_RESTRICTING; }
    }
    else if (m_createState == CREATE_RESTRICTING)
    {
        // See how many cores we REALLY need.
        Family& family = m_familyTable[info.fid];

        // Exclusive families and families with shareds run on one core.
        // Non-exclusive families without cores are distributed, but cannot use more cores than it has threads.
        const PSize numCores = (m_familyTable.IsExclusive(info.fid) || family.hasShareds)
            ? 1
            : std::max<Integer>(1, std::min<Integer>(family.nThreads, family.numCores));

        // We now know how many threads and cores we really have,
        // calculate and set up the thread distribution

        CalculateDistribution(family, family.nThreads, numCores);

        // Log the event where we reduced the number of cores
        DebugSimWrite("F%u (local %llu threads, start index %llu, physical block size %u) adjusted core count %u -> %u",
                      (unsigned)info.fid, (unsigned long long)family.nThreads,
                      (unsigned long long)family.start, (unsigned)family.physBlockSize,
                      (unsigned)family.numCores, (unsigned)numCores);

        COMMIT
        {
            family.numCores = numCores;

            // Advance to next stage
            m_createState = CREATE_ALLOCATING_REGISTERS;
        }
    }
    else if (m_createState == CREATE_ALLOCATING_REGISTERS)
    {
        // Allocate the registers
        ContextType type = m_familyTable.IsExclusive(info.fid) ? CONTEXT_EXCLUSIVE : CONTEXT_RESERVED;
        if (!AllocateRegisters(info.fid, type))
        {
            DeadlockWrite("Unable to allocate registers for F%u", (unsigned)info.fid);
            return FAILED;
        }

        // Advance to next stage
        COMMIT{ m_createState = CREATE_BROADCASTING_CREATE; }
    }
    else if (m_createState == CREATE_BROADCASTING_CREATE)
    {
        // Broadcast the create
        Family& family = m_familyTable[info.fid];
        if (family.link != INVALID_LFID)
        {
            LinkMessage msg;
            msg.type            = LinkMessage::MSG_CREATE;
            msg.create.fid      = family.link;
            msg.create.numCores = family.numCores - 1;
            msg.create.address  = family.pc;
            for (size_t i = 0; i < NUM_REG_TYPES; i++)
            {
                msg.create.regs[i] = family.regs[i].count;
            }

            if (!m_network.SendMessage(msg))
            {
                DeadlockWrite("Unable to send the create for F%u", (unsigned)info.fid);
                return FAILED;
            }

            if (family.numCores == 1)
            {
                // We've reduced the number of cores to one, clear link
                COMMIT{ family.link = INVALID_LFID; }
            }
        }

        // Advance to next stage
        COMMIT{ m_createState = CREATE_ACTIVATING_FAMILY; }
    }
    else if (m_createState == CREATE_ACTIVATING_FAMILY)
    {
        Family& family  = m_familyTable[info.fid];

        if (info.bundle)
        {
                assert(family.nThreads > 0);
                if (family.regs[0].count.shareds < 1)
                {
                    throw exceptf<SimulationException>("Program target of bundle create does not define shared registers");
                }

                RegAddr  addr   = MAKE_REGADDR(RT_INTEGER, family.regs[0].last_shareds);
                RegValue data;
                data.m_state    = RST_FULL;
                data.m_integer  = info.parameter;

                if (!m_registerFile.p_asyncW.Write(addr))
                {
                    DeadlockWrite("Unable to acquire Register File port");
                    return FAILED;
                }

                if (!m_registerFile.WriteRegister(addr, data, false))
                {
                    DeadlockWrite("Unable to write shareds for bundle creation");
                    return FAILED;
                }
            else
            {
                DebugSimWrite("Set shareds of F%u at R%u to value %lld",
                              (unsigned)info.fid, (unsigned)family.regs[0].last_shareds, (long long)info.parameter);
            }

            if(info.completion_reg == INVALID_REG_INDEX)
            {
                family.dependencies.detached         = true;
                family.dependencies.syncSent         = true;
            }
        }

        if (family.nThreads == 0)
        {
            if (!m_familyTable.IsExclusive(info.fid))
            {
                DebugSimWrite("F%u is empty, deallocating reserved thread entry", (unsigned)info.fid);
                m_threadTable.UnreserveThread();
            }

            // We're done allocating threads
            if (!DecreaseFamilyDependency(info.fid, FAMDEP_ALLOCATION_DONE))
            {
                DeadlockWrite("F%u unable to mark ALLOCATION_DONE", (unsigned)info.fid);
                return FAILED;
            }
        }
        else
        {
            // We can start creating threads
            if (!ActivateFamily(info.fid))
            {
                DeadlockWrite("Unable to activate the family F%u", (unsigned)info.fid);
                return FAILED;
            }
            DebugSimWrite("F%u activated", (unsigned)info.fid);
        }

        COMMIT{ m_createState = CREATE_NOTIFY; }
    }
    else if (m_createState == CREATE_NOTIFY)
    {
        // Notify creator
        if (info.completion_reg != INVALID_REG_INDEX)
        {
            FID fid;
            fid.lfid       = info.fid;
            fid.pid        = m_parent.GetPID();
            fid.capability = m_familyTable[info.fid].capability;

            RemoteMessage msg;
            msg.type                    = RemoteMessage::MSG_RAW_REGISTER;
            msg.rawreg.pid              = info.completion_pid;
            msg.rawreg.addr             = MAKE_REGADDR(RT_INTEGER, info.completion_reg);
            msg.rawreg.value.m_state    = RST_FULL;
            msg.rawreg.value.m_integer  = m_parent.PackFID(fid);

            if (!m_network.SendMessage(msg))
            {
                DeadlockWrite("Unable to send creation completion to CPU%u", (unsigned)info.completion_pid);
                return FAILED;
            }
            DebugSimWrite("F%u sent create writeback to CPU%u/R%04x",
                          (unsigned)info.fid, (unsigned)info.completion_pid, (unsigned)info.completion_reg);
        }
        DebugSimWrite("F%u create notified", (unsigned)info.fid);

        // Reset the create state
        COMMIT{ m_createState = CREATE_INITIAL; }

        m_creates.Pop();
    }

    return SUCCESS;
}

Result Processor::Allocator::DoThreadActivation()
{
    TID tid;
    if ((m_prevReadyList == &m_readyThreads2 || m_readyThreads2.Empty()) && !m_readyThreads1.Empty()) {
        tid = m_readyThreads1.Front();
        m_readyThreads1.Pop();
        COMMIT{ m_prevReadyList = &m_readyThreads1; }
    } else {
        assert(!m_readyThreads2.Empty());
        tid = m_readyThreads2.Front();
        m_readyThreads2.Pop();
        COMMIT{ m_prevReadyList = &m_readyThreads2; }
    }
    COMMIT{ --m_numThreadsPerState[TST_READY]; }

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
                thread.next  = next;
                thread.state = TST_WAITING;
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

// Sanitizes the limit and block size.
// Use only for non-delegated creates.
Integer Processor::Allocator::CalculateThreadCount(Integer start, Integer limit, Integer step)
{
    // Sanitize the family entry
    if (step == 0)
    {
        throw SimulationException("Step cannot be zero", *this);
    }

    Integer diff = 0;
    if (step > 0)
    {
        if (limit > start) {
            diff = limit - start;
        }
    } else {
        if (limit < start) {
            diff = start - limit;
        }
        step = -step;
    }

    // Divide threads evenly over the cores
    return (diff + step - 1) / step;
}

void Processor::Allocator::CalculateDistribution(Family& family, Integer nThreads, PSize numCores)
{
    Integer threadsPerCore = std::max<Integer>(1, (nThreads + numCores - 1) / numCores);

    // If the numCores is 1, the family can start inside a place, so we can't use
    // placeSize to calculate the skip. The skip is 0 in that case.
    Integer nThreadsSkipped = (numCores > 1) ? threadsPerCore * (m_parent.GetPID() % family.placeSize) : 0;

    // Calculate number of threads to run on this core
    nThreads = std::min(std::max(nThreads, nThreadsSkipped) - nThreadsSkipped, threadsPerCore);

    // Establish the physical block size
    TSize blockSize = (family.physBlockSize == 0)
        ? m_threadTable.GetNumThreads() - 1 // Default block size, use the maximum: the thread table size (minus the exclusive context)
        : family.physBlockSize;

    // Restrict physical block size based on number of threads on the core
    family.physBlockSize = std::min<Integer>(blockSize, nThreads);

    COMMIT
    {
        // Skip the threads that run before us
        family.start   += family.step * nThreadsSkipped;
        family.nThreads = nThreads;
    }
}

Processor::Allocator::Allocator(const string& name, Processor& parent, Clock& clock,
    FamilyTable& familyTable, ThreadTable& threadTable, RegisterFile& registerFile, RAUnit& raunit, ICache& icache, DCache& dcache, Network& network, Pipeline& pipeline,
    Config& config)
 :  Object(name, parent, clock),
    m_parent(parent), m_familyTable(familyTable), m_threadTable(threadTable), m_registerFile(registerFile), m_raunit(raunit), m_icache(icache), m_dcache(dcache), m_network(network), m_pipeline(pipeline),
    m_bundle        ("b_indirectcreate", *this, clock, config.getValueOrDefault<BufferSize>(*this,"IndirectCreateQueueSize", 8)),
    m_alloc         ("b_alloc",          *this, clock, config.getValueOrDefault<BufferSize>(*this, "InitialThreadAllocateQueueSize", familyTable.GetNumFamilies())),
    m_creates       ("b_creates",        *this, clock, config.getValueOrDefault<BufferSize>(*this, "CreateQueueSize", familyTable.GetNumFamilies()), 3),
    m_cleanup       ("b_cleanup",        *this, clock, config.getValueOrDefault<BufferSize>(*this, "ThreadCleanupQueueSize", threadTable.GetNumThreads()), 4),
    m_createState   (CREATE_INITIAL),
    m_createLine    (0),
    m_readyThreads1 ("q_readyThreads1", *this, clock, threadTable),
    m_readyThreads2 ("q_readyThreads2", *this, clock, threadTable),
    m_prevReadyList (NULL),

    m_allocRequestsSuspend  ("b_allocRequestsSuspend",   *this, clock, config.getValue<BufferSize>(*this, "FamilyAllocationSuspendQueueSize")),
    m_allocRequestsNoSuspend("b_allocRequestsNoSuspend", *this, clock, config.getValue<BufferSize>(*this, "FamilyAllocationNoSuspendQueueSize")),
    m_allocRequestsExclusive("b_allocRequestsExclusive", *this, clock, config.getValue<BufferSize>(*this, "FamilyAllocationExclusiveQueueSize")),

    m_bundleState   (BUNDLE_INITIAL),

    m_lastcycle(0), m_maxallocex(0), m_totalallocex(0), m_curallocex(0),
    m_numCreatedFamilies(0),
    m_numCreatedThreads(0),

    p_ThreadAllocate  (*this, "thread-allocate",   delegate::create<Allocator, &Processor::Allocator::DoThreadAllocate  >(*this) ),
    p_FamilyAllocate  (*this, "family-allocate",   delegate::create<Allocator, &Processor::Allocator::DoFamilyAllocate  >(*this) ),
    p_FamilyCreate    (*this, "family-create",     delegate::create<Allocator, &Processor::Allocator::DoFamilyCreate    >(*this) ),
    p_ThreadActivation(*this, "thread-activation", delegate::create<Allocator, &Processor::Allocator::DoThreadActivation>(*this) ),
    p_Bundle          (*this, "bundle-Create",     delegate::create<Allocator, &Processor::Allocator::DoBundle          >(*this) ),

    p_allocation    (*this, clock, "p_allocation"),
    p_alloc         (*this, clock, "p_alloc"),
    p_readyThreads  (*this, clock, "p_readyThreads"),
    p_activeThreads (*this, clock, "p_activeThreads"),
    m_activeThreads ("q_threadList", *this, clock, threadTable)
{
    m_alloc         .Sensitive(p_ThreadAllocate);
    m_creates       .Sensitive(p_FamilyCreate);
    m_cleanup       .Sensitive(p_ThreadAllocate);
    m_readyThreads1 .Sensitive(p_ThreadActivation);
    m_readyThreads2 .Sensitive(p_ThreadActivation);
    m_activeThreads .Sensitive(pipeline.p_Pipeline); // Fetch Stage is sensitive on this list

    m_allocRequestsSuspend  .Sensitive(p_FamilyAllocate);
    m_allocRequestsNoSuspend.Sensitive(p_FamilyAllocate);
    m_allocRequestsExclusive.Sensitive(p_FamilyAllocate);
    m_bundle                .Sensitive(p_Bundle);

    std::fill(m_numThreadsPerState, m_numThreadsPerState+TST_NUMSTATES, 0);

    RegisterSampleVariableInObject(m_totalallocex, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_maxallocex, SVC_WATERMARK);
    RegisterSampleVariableInObject(m_curallocex, SVC_LEVEL);
    RegisterSampleVariableInObject(m_numCreatedFamilies, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numCreatedThreads, SVC_CUMULATIVE);
    RegisterSampleVariableInObjectWithName(m_numThreadsPerState[TST_ACTIVE], "m_numActiveThreads", SVC_LEVEL);
    RegisterSampleVariableInObjectWithName(m_numThreadsPerState[TST_READY], "m_numReadyThreads", SVC_LEVEL);
}

void Processor::Allocator::AllocateInitialFamily(MemAddr pc, bool legacy, PSize placeSize, SInteger startIndex)
{
    static const unsigned char InitialRegisters[NUM_REG_TYPES] = {31, 31};

    LFID fid = m_familyTable.AllocateFamily(CONTEXT_NORMAL);
    if (fid == INVALID_LFID)
    {
        throw SimulationException("Unable to create initial family", *this);
    }

    InitializeFamily(fid);

    Family& family = m_familyTable[fid];
    family.numCores      = 1;
    family.placeSize     = placeSize;
    family.nThreads      = 1;
    //family.virtBlockSize = 1;
    family.physBlockSize = 1;
    family.legacy        = legacy;
    family.pc            = pc;
    family.state         = FST_ACTIVE;
    family.start         = startIndex;

    for (size_t i = 0; i < NUM_REG_TYPES; i++)
    {
        family.regs[i].count.locals  = InitialRegisters[i];
        family.regs[i].count.globals = 0;
        family.regs[i].count.shareds = 0;
    }

    // The main family starts off detached
    family.dependencies.prevSynchronized = true;
    family.dependencies.detached = true;
    family.dependencies.syncSent = true;

    if (!AllocateRegisters(fid, CONTEXT_NORMAL))
    {
        throw SimulationException("Unable to create initial family", *this);
    }

    m_threadTable.ReserveThread();

    m_alloc.Push(fid);
}

void Processor::Allocator::Push(ThreadQueue& q, TID tid)
{
    COMMIT
    {
        if (q.head == INVALID_TID) {
            q.head = tid;
        } else {
            m_threadTable[q.tail].next = tid;
        }
        q.tail = tid;
    }
}

TID Processor::Allocator::Pop(ThreadQueue& q)
{
    TID tid = q.head;
    if (q.head != INVALID_TID)
    {
        q.head = m_threadTable[tid].next;
    }
    return tid;
}

void Processor::Allocator::Cmd_Info(ostream& out, const vector<string>& /* arguments */) const
{
    out <<
    "The Allocator is where most of the thread and family management takes place.\n"
    "\n"
    "Supported operations:\n"
    "- inspect <component> [range]\n"
    "  Reads and displays the various queues and registers in the Allocator.\n";
}

void Processor::Allocator::Cmd_Read(ostream& out, const vector<string>& /*arguments*/) const
{
    {
        const struct {
            const char*                 type;
            const Buffer<AllocRequest>& queue;
        } Queues[3] = {
            {"Suspend",     m_allocRequestsSuspend},
            {"Non-suspend", m_allocRequestsNoSuspend},
            {"Exclusive",   m_allocRequestsExclusive},
        };

        for (int i = 0; i < 3; ++i)
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
                    out << p->placeSize << " cores (R" << hex << uppercase << setw(4) << setfill('0') << p->completion_reg
                        << "@P" << dec << nouppercase << p->completion_pid << "); type: " << AllocationTypes[p->type];
                    if (++p != allocations.end()) {
                        out << "; ";
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
                if (p->completion_reg != INVALID_REG_INDEX) {
                    out << " (R" << p->completion_reg << "@CPU" << p->completion_pid << ")";
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
                case CREATE_LOAD_REGSPEC:         out << "Looking for regspec"; break;
                case CREATE_LOADING_LINE:         out << "Loading cache-line"; break;
                case CREATE_LINE_LOADED:          out << "Cache-line loaded"; break;
                case CREATE_RESTRICTING:          out << "Restricting"; break;
                case CREATE_ALLOCATING_REGISTERS: out << "Allocating registers"; break;
                case CREATE_BROADCASTING_CREATE:  out << "Broadcasting create"; break;
                case CREATE_ACTIVATING_FAMILY:    out << "Activating family"; break;
                case CREATE_NOTIFY:               out << "Notifying creator"; break;
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
                if (++p != m_cleanup.end())
                {
                    out << ", ";
                }
            }
            out << endl;
        }
    }
}

}
