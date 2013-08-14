#include "DCache.h"
#include "DRISC.h"
#include <sim/log2.h>
#include <sim/config.h>
#include <sim/sampling.h>

#include <cassert>
#include <cstring>
#include <iomanip>
#include <cstdio>
using namespace std;

namespace Simulator
{

namespace drisc
{

DCache::DCache(const std::string& name, DRISC& parent, Clock& clock, Config& config)
:   Object(name, parent, clock),
    m_memory(NULL),
    m_mcid(0),
    m_lines(),

    m_assoc          (config.getValue<size_t>(*this, "Associativity")),
    m_sets           (config.getValue<size_t>(*this, "NumSets")),
    m_lineSize       (config.getValue<size_t>("CacheLineSize")),
    m_selector       (IBankSelector::makeSelector(*this, config.getValue<string>(*this, "BankSelector"), m_sets)),
    m_read_responses ("b_read_responses", *this, clock, config.getValue<BufferSize>(*this, "ReadResponsesBufferSize")),
    m_write_responses("b_write_responses", *this, clock, config.getValue<BufferSize>(*this, "WriteResponsesBufferSize")),
    m_writebacks     ("b_writebacks", *this, clock, config.getValue<BufferSize>(*this, "ReadWritebacksBufferSize")),
    m_outgoing       ("b_outgoing", *this, clock, config.getValue<BufferSize>(*this, "OutgoingBufferSize")),
    m_wbstate(),
    m_numRHits        (0),
    m_numDelayedReads (0),
    m_numEmptyRMisses (0),
    m_numInvalidRMisses(0),
    m_numLoadingRMisses(0),
    m_numHardConflicts(0),
    m_numResolvedConflicts(0),
    m_numWAccesses    (0),
    m_numWHits        (0),
    m_numPassThroughWMisses(0),
    m_numLoadingWMisses(0),
    m_numStallingRMisses(0),
    m_numStallingWMisses(0),
    m_numSnoops(0),

    p_ReadWritebacks(*this, "read-writebacks", delegate::create<DCache, &DCache::DoReadWritebacks  >(*this) ),
    p_ReadResponses (*this, "read-responses",  delegate::create<DCache, &DCache::DoReadResponses   >(*this) ),
    p_WriteResponses(*this, "write-responses", delegate::create<DCache, &DCache::DoWriteResponses  >(*this) ),
    p_Outgoing      (*this, "outgoing",        delegate::create<DCache, &DCache::DoOutgoingRequests>(*this) ),

    p_service        (*this, clock, "p_service")
{
    RegisterSampleVariableInObject(m_numRHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numEmptyRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numLoadingRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numInvalidRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numHardConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numResolvedConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numWHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numLoadingWMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingRMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingWMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numPassThroughWMisses, SVC_CUMULATIVE);

    m_writebacks.Sensitive(p_ReadWritebacks);
    m_read_responses.Sensitive(p_ReadResponses);
    m_write_responses.Sensitive(p_WriteResponses);
    m_outgoing.Sensitive(p_Outgoing);

    // These things must be powers of two
    if (m_assoc == 0 || !IsPowerOfTwo(m_assoc))
    {
        throw exceptf<InvalidArgumentException>(*this, "DCacheAssociativity = %zd is not a power of two", (size_t)m_assoc);
    }

    if (m_sets == 0 || !IsPowerOfTwo(m_sets))
    {
        throw exceptf<InvalidArgumentException>(*this, "DCacheNumSets = %zd is not a power of two", (size_t)m_sets);
    }

    if (m_lineSize == 0 || !IsPowerOfTwo(m_lineSize))
    {
        throw exceptf<InvalidArgumentException>(*this, "CacheLineSize = %zd is not a power of two", (size_t)m_lineSize);
    }

    // At least a complete register value has to fit in a line
    if (m_lineSize < 8)
    {
        throw exceptf<InvalidArgumentException>(*this, "CacheLineSize = %zd is less than 8.", (size_t)m_lineSize);
    }

    m_lines.resize(m_sets * m_assoc);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        m_lines[i].state  = LINE_EMPTY;
        m_lines[i].data   = new char[m_lineSize];
        m_lines[i].valid  = new bool[m_lineSize];
        m_lines[i].create = false;
    }

    m_wbstate.size   = 0;
    m_wbstate.offset = 0;
}

void DCache::ConnectMemory(IMemory* memory)
{
    assert(memory != NULL);
    assert(m_memory == NULL); // can't register two times

    m_memory = memory;
    StorageTraceSet traces;
    m_mcid = m_memory->RegisterClient(*this, p_Outgoing, traces, m_read_responses ^ m_write_responses, true);
    p_Outgoing.SetStorageTraces(traces);

}

DCache::~DCache()
{
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        delete[] m_lines[i].data;
        delete[] m_lines[i].valid;
    }
    delete m_selector;
}

Result DCache::FindLine(MemAddr address, Line* &line, bool check_only)
{
    MemAddr tag;
    size_t setindex;
    m_selector->Map(address / m_lineSize, tag, setindex);
    const size_t  set  = setindex * m_assoc;

    // Find the line
    Line* empty   = NULL;
    Line* replace = NULL;
    for (size_t i = 0; i < m_assoc; ++i)
    {
        line = &m_lines[set + i];

        // Invalid lines may not be touched or considered
        if (line->state == LINE_EMPTY)
        {
            // Empty, unused line, remember this one
            empty = line;
        }
        else if (line->tag == tag)
        {
            // The wanted line was in the cache
            return SUCCESS;
        }
        else if (line->state == LINE_FULL && (replace == NULL || line->access < replace->access))
        {
            // The line is available to be replaced and has a lower LRU rating,
            // remember it for replacing
            replace = line;
        }
    }

    // The line could not be found, allocate the empty line or replace an existing line
    line = (empty != NULL) ? empty : replace;
    if (line == NULL)
    {
        // No available line
        if (!check_only)
        {
            DeadlockWrite("Unable to allocate a free cache-line in set %u", (unsigned)(set / m_assoc) );
        }
        return FAILED;
    }

    if (!check_only)
    {
        // Reset the line
        COMMIT
        {
            line->processing = false;
            line->tag        = tag;
            line->waiting    = INVALID_REG;
            std::fill(line->valid, line->valid + m_lineSize, false);
        }
    }

    return DELAYED;
}



Result DCache::Read(MemAddr address, void* data, MemSize size, RegAddr* reg)
{
    size_t offset = (size_t)(address % m_lineSize);
    if (offset + size > m_lineSize)
    {
        throw exceptf<InvalidArgumentException>(*this, "Read (%#016llx, %zd): Address range crosses over cache line boundary",
                                                (unsigned long long)address, (size_t)size);
    }

#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw exceptf<InvalidArgumentException>(*this, "Read (%#016llx, %zd): Size argument too big",
                                                (unsigned long long)address, (size_t)size);
    }
#endif

    // Check that we're reading readable memory
    auto& cpu = GetDRISC();
    if (!cpu.CheckPermissions(address, size, IMemory::PERM_READ))
    {
        throw exceptf<SecurityException>(*this, "Read (%#016llx, %zd): Attempting to read from non-readable memory",
                                         (unsigned long long)address, (size_t)size);
    }

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for D-Cache read access (%#016llx, %zd)",
                      (unsigned long long)address, (size_t)size);

        return FAILED;
    }

    Line*  line;
    Result result;
    // SUCCESS - A line with the address was found
    // DELAYED - The line with the address was not found, but a line has been allocated
    // FAILED  - No usable line was found at all and could not be allocated
    if ((result = FindLine(address - offset, line, false)) == FAILED)
    {
        // Cache-miss and no free line
        // DeadlockWrite() is done in FindLine
        ++m_numHardConflicts;
        return FAILED;
    }

    // Update last line access
    COMMIT{ line->access = GetCycleNo(); }

    if (result == DELAYED)
    {
        // A new line has been allocated; send the request to memory
        Request request;
        request.write     = false;
        request.address   = address - offset;
        if (!m_outgoing.Push(request))
        {
            ++m_numStallingRMisses;
            DeadlockWrite("Unable to push request to outgoing buffer");
            return FAILED;
        }

        // statistics
        COMMIT {
            if (line->state == LINE_EMPTY)
                ++m_numEmptyRMisses;
            else
                ++m_numResolvedConflicts;
        }
    }
    else
    {
        // Check if the data that we want is valid in the line.
        // This happens when the line is FULL, or LOADING and has been
        // snooped to (written to from another core) in the mean time.
        size_t i;
        for (i = 0; i < size; ++i)
        {
            if (!line->valid[offset + i])
            {
                break;
            }
        }

        if (i == size)
        {
            // Data is entirely in the cache, copy it
            COMMIT
            {
                memcpy(data, line->data + offset, (size_t)size);
                ++m_numRHits;
            }
            return SUCCESS;
        }

        // Data is not entirely in the cache; it should be loading from memory
        if (line->state != LINE_LOADING)
        {
            assert(line->state == LINE_INVALID);
            ++m_numInvalidRMisses;
            return FAILED;
        }
        else
        {
            COMMIT{ ++m_numLoadingRMisses; }
        }
    }

    // Data is being loaded, add request to the queue
    COMMIT
    {
        line->state = LINE_LOADING;
        if (reg != NULL && reg->valid())
        {
            // We're loading to a valid register, queue it
            RegAddr old   = line->waiting;
            line->waiting = *reg;
            *reg = old;
        }
        else
        {
            line->create  = true;
        }

        // Statistics:
        ++m_numDelayedReads;
    }
    return DELAYED;
}

Result DCache::Write(MemAddr address, void* data, MemSize size, LFID fid, TID tid)
{
    assert(fid != INVALID_LFID);
    assert(tid != INVALID_TID);

    size_t offset = (size_t)(address % m_lineSize);
    if (offset + size > m_lineSize)
    {
        throw exceptf<InvalidArgumentException>(*this, "Write (%#016llx, %zd): Address range crosses over cache line boundary",
                                                (unsigned long long)address, (size_t)size);
    }

#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw exceptf<InvalidArgumentException>(*this, "Write (%#016llx, %zd): Size argument too big",
                                                (unsigned long long)address, (size_t)size);
    }
#endif

    // Check that we're writing writable memory
    auto& cpu = GetDRISC();
    if (!cpu.CheckPermissions(address, size, IMemory::PERM_WRITE))
    {
        throw exceptf<SecurityException>(*this, "Write (%#016llx, %zd): Attempting to write to non-writable memory",
                                         (unsigned long long)address, (size_t)size);
    }

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for D-Cache write access (%#016llx, %zd)",
                      (unsigned long long)address, (size_t)size);
        return FAILED;
    }

    Line* line = NULL;
    Result result = FindLine(address, line, true);
    if (result == SUCCESS)
    {
        assert(line->state != LINE_EMPTY);

        if (line->state == LINE_LOADING || line->state == LINE_INVALID)
        {
            // We cannot write into a loading line or we might violate the
            // sequential semantics of a single thread because pending reads
            // might get the later write's data.
            // We cannot ignore the line either because new reads should get the new
            // data and not the old.
            // We cannot invalidate the line so new reads will generate a new
            // request, because read completion goes on address, and then we would have
            // multiple lines of the same address.
            //

            // So for now, just stall the write
            ++m_numLoadingWMisses;
            DeadlockWrite("Unable to write into loading cache line");
            return FAILED;
        }
        else
        {
            // Update the line
            assert(line->state == LINE_FULL);
            COMMIT{
                std::copy((char*)data, (char*)data + size, line->data + offset);
                std::fill(line->valid + offset, line->valid + offset + size, true);

                // Statistics
                ++m_numWHits;
            }
        }
    }
    else
    {
        COMMIT{ ++m_numPassThroughWMisses; }
    }

    // Store request for memory (pass-through)
    Request request;
    request.write     = true;
    request.address   = address - offset;
    request.wid       = tid;

    COMMIT{
    std::copy((char*)data, ((char*)data)+size, request.data.data+offset);
    std::fill(request.data.mask, request.data.mask+offset, false);
    std::fill(request.data.mask+offset, request.data.mask+offset+size, true);
    std::fill(request.data.mask+offset+size, request.data.mask+m_lineSize, false);
    }

    if (!m_outgoing.Push(request))
    {
        ++m_numStallingWMisses;
        DeadlockWrite("Unable to push request to outgoing buffer");
        return FAILED;
    }

    COMMIT{ ++m_numWAccesses; }

    return DELAYED;
}

bool DCache::OnMemoryReadCompleted(MemAddr addr, const char* data)
{
    // Check if we have the line and if its loading.
    // This method gets called whenever a memory read completion is put on the
    // bus from memory, so we have to check if we actually need the data.
    Line* line;
    if (FindLine(addr, line, true) == SUCCESS && line->state != LINE_FULL && !line->processing)
    {
        assert(line->state == LINE_LOADING || line->state == LINE_INVALID);

        // Registers are waiting on this data
        COMMIT
        {
            /*
                      Merge with pending writes because the incoming line
                      will not know about those yet and we don't want inconsistent
                      content in L1.
                      This is kind of a hack; it's feasibility in hardware in a single cycle
                      is questionable.
            */
            char mdata[m_lineSize];

            std::copy(data, data + m_lineSize, mdata);

            for (Buffer<Request>::const_iterator p = m_outgoing.begin(); p != m_outgoing.end(); ++p)
            {
                if (p->write && p->address == addr)
                {
                    // This is a write to the same line, merge it
                    line::blit(&mdata[0], p->data.data, p->data.mask, m_lineSize);
                }
            }

            // Copy the data into the cache line.
            // Mask by valid bytes (don't overwrite already written data).
            line::blitnot(line->data, mdata, line->valid, m_lineSize);
            line::setifnot(line->valid, true, line->valid, m_lineSize);

            line->processing = true;
        }

        // Push the cache-line to the back of the queue
        ReadResponse response;
        response.cid   = line - &m_lines[0];

        DebugMemWrite("Received read completion for %#016llx -> CID %u", (unsigned long long)addr, (unsigned)response.cid);

        if (!m_read_responses.Push(response))
        {
            DeadlockWrite("Unable to push read completion to buffer");
            return false;
        }
    }
    return true;
}

bool DCache::OnMemoryWriteCompleted(WClientID wid)
{
    // Data has been written
    if (wid != INVALID_WCLIENTID) // otherwise for DCA
    {
        DebugMemWrite("Received write completion for client %u", (unsigned)wid);

        WriteResponse response;
        response.wid  =  wid;
        if (!m_write_responses.Push(response))
        {
            DeadlockWrite("Unable to push write completion to buffer");
            return false;
        }
    }
    return true;
}

bool DCache::OnMemorySnooped(MemAddr address, const char* data, const bool* mask)
{
    Line*  line;

    // FIXME: snoops should really either lock the line or access
    // through a different port. Here we cannot (yet) invoke the
    // arbitrator because there is no scaffolding to declare which
    // processes can call OnMemorySnooped via AddProcess().
    /*
    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for D-Cache snoop access (%#016llx, %zd)",
                      (unsigned long long)address, (size_t)data.size);

        return false;
    }
    */

    // Cache coherency: check if we have the same address
    if (FindLine(address, line, true) == SUCCESS)
    {
        DebugMemWrite("Received snoop request for loaded line %#016llx", (unsigned long long)address);

        COMMIT
        {
            // We do, update the data and mark written bytes as valid
            // Note that we don't have to check against already written data or queued reads
            // because we don't have to guarantee sequential semantics from other cores.
            // This falls within the non-determinism behavior of the architecture.
            line::blit(line->data, data, mask, m_lineSize);
            line::setif(line->valid, true, mask, m_lineSize);

            // Statistics
            ++m_numSnoops;
        }
    }
    return true;
}

bool DCache::OnMemoryInvalidated(MemAddr address)
{
    COMMIT
    {
        Line* line;
        if (FindLine(address, line, true) == SUCCESS)
        {
            DebugMemWrite("Received invalidation request for loaded line %#016llx", (unsigned long long)address);

            // We have the line, invalidate it
            if (line->state == LINE_FULL) {
                // Full lines are invalidated by clearing them. Simple.
                line->state = LINE_EMPTY;
            } else if (line->state == LINE_LOADING) {
                // The data is being loaded. Invalidate the line and it will get cleaned up
                // when the data is read.
                line->state = LINE_INVALID;
            }
        }
    }
    return true;
}

Object& DCache::GetMemoryPeer()
{
    return *GetParent();
}

Result DCache::DoReadResponses()
{
    assert(!m_read_responses.Empty());

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for D-Cache access in read completion");
        return FAILED;
    }

    // Process a waiting register
    auto& response = m_read_responses.Front();
    Line& line = m_lines[response.cid];
    assert(line.state == LINE_LOADING || line.state == LINE_INVALID);

    DebugMemWrite("Processing read completion for CID %u", (unsigned)response.cid);

    // If bundle creation is waiting for the line data, deliver it
    if (line.create)
    {
        DebugMemWrite("Signalling read completion to creation process");
        auto& alloc = GetDRISC().GetAllocator();
        alloc.OnDCachelineLoaded(line.data);
        COMMIT { line.create = false; }
    }

    if (line.waiting.valid())
    {
        // Push the cache-line to the back of the queue
        WritebackRequest req;
        std::copy(line.data, line.data + m_lineSize, req.data);
        req.waiting = line.waiting;

        DebugMemWrite("Queuing writeback request for CID %u starting at %s", (unsigned)response.cid, req.waiting.str().c_str());

        if (!m_writebacks.Push(req))
        {
            DeadlockWrite("Unable to push writeback request to buffer");
            return FAILED;
        }
    }

    COMMIT {
        line.waiting = INVALID_REG;
        line.state = (line.state == LINE_INVALID) ? LINE_EMPTY : LINE_FULL;
    }
    m_read_responses.Pop();
    return SUCCESS;
}

Result DCache::DoReadWritebacks()
{
    assert(!m_writebacks.Empty());

    // Process a waiting register
    auto& req = m_writebacks.Front();

    WritebackState state = m_wbstate;
    if (!state.next.valid())
    {
        // New request
        assert(req.waiting.valid());
        state.next = req.waiting;
    }

    auto& regFile = GetDRISC().GetRegisterFile();

    if (state.offset == state.size)
    {
        // Starting a new multi-register write

        // Write to register
        if (!regFile.p_asyncW.Write(state.next))
        {
            DeadlockWrite("Unable to acquire port to write back %s", state.next.str().c_str());
            return FAILED;
        }

        // Read request information
        RegValue value;
        if (!regFile.ReadRegister(state.next, value))
        {
            DeadlockWrite("Unable to read register %s", state.next.str().c_str());
            return FAILED;
        }

        if (value.m_state == RST_FULL || value.m_memory.size == 0)
        {
            // Rare case: the request info is still in the pipeline, stall!
            DeadlockWrite("Register %s is not yet written for read completion", state.next.str().c_str());
            return FAILED;
        }

        if (value.m_state != RST_PENDING && value.m_state != RST_WAITING)
        {
            // We're too fast, wait!
            DeadlockWrite("Memory read completed before register %s was cleared", state.next.str().c_str());
            return FAILED;
        }

        // Ignore the request if the family has been killed
        state.value = UnserializeRegister(state.next.type, &req.data[value.m_memory.offset], value.m_memory.size);

        if (value.m_memory.sign_extend)
        {
            // Sign-extend the value
            assert(value.m_memory.size < sizeof(Integer));
            int shift = (sizeof(state.value) - value.m_memory.size) * 8;
            state.value = (int64_t)(state.value << shift) >> shift;
        }

        state.fid    = value.m_memory.fid;
        state.addr   = state.next;
        state.next   = value.m_memory.next;
        state.offset = 0;

        // Number of registers that we're writing (must be a power of two)
        state.size = (value.m_memory.size + sizeof(Integer) - 1) / sizeof(Integer);
        assert((state.size & (state.size - 1)) == 0);
    }
    else
    {
        // Write to register
        if (!regFile.p_asyncW.Write(state.addr))
        {
            DeadlockWrite("Unable to acquire port to write back %s", state.addr.str().c_str());
            return FAILED;
        }
    }

    assert(state.offset < state.size);

    // Write to register file
    RegValue reg;
    reg.m_state = RST_FULL;

#if ARCH_ENDIANNESS == ARCH_BIG_ENDIAN
    // LSB goes in last register
    const Integer data = state.value >> ((state.size - 1 - state.offset) * sizeof(Integer) * 8);
#else
    // LSB goes in first register
    const Integer data = state.value >> (state.offset * sizeof(Integer) * 8);
#endif

    switch (state.addr.type) {
    case RT_INTEGER: reg.m_integer       = data; break;
    case RT_FLOAT:   reg.m_float.integer = data; break;
    default: UNREACHABLE;
    }

    DebugMemWrite("Completed load: %#016llx -> %s %s",
                  (unsigned long long)data, state.addr.str().c_str(), reg.str(state.addr.type).c_str());

    if (!regFile.WriteRegister(state.addr, reg, true))
    {
        DeadlockWrite("Unable to write register %s", state.addr.str().c_str());
        return FAILED;
    }

    // Update writeback state
    state.offset++;
    state.addr.index++;

    if (state.offset == state.size)
    {
        // This operand is now fully written
        auto& alloc = GetDRISC().GetAllocator();
        if (!alloc.DecreaseFamilyDependency(state.fid, FAMDEP_OUTSTANDING_READS))
        {
            DeadlockWrite("Unable to decrement outstanding reads on F%u", (unsigned)state.fid);
            return FAILED;
        }

        if (!state.next.valid())
        {
            m_writebacks.Pop();
        }
    }
    COMMIT{ m_wbstate = state; }

    return SUCCESS;
}

Result DCache::DoWriteResponses()
{
    assert(!m_write_responses.Empty());
    auto& response = m_write_responses.Front();

    auto& alloc = GetDRISC().GetAllocator();
    if (!alloc.DecreaseThreadDependency((TID)response.wid, THREADDEP_OUTSTANDING_WRITES))
    {
        DeadlockWrite("Unable to decrease outstanding writes on T%u", (unsigned)response.wid);
        return FAILED;
    }

    DebugMemWrite("T%u completed store", (unsigned)response.wid);

    m_write_responses.Pop();
    return SUCCESS;
}

Result DCache::DoOutgoingRequests()
{
    assert(m_memory != NULL);
    assert(!m_outgoing.Empty());
    const Request& request = m_outgoing.Front();

    if (request.write)
    {
        if (!m_memory->Write(m_mcid, request.address, request.data, request.wid))
        {
            DeadlockWrite("Unable to send write to 0x%016llx to memory", (unsigned long long)request.address);
            return FAILED;
        }
    }
    else
    {
        if (!m_memory->Read(m_mcid, request.address))
        {
            DeadlockWrite("Unable to send read to 0x%016llx to memory", (unsigned long long)request.address);
            return FAILED;
        }
    }

    DebugMemWrite("F%d queued outgoing %s request for %.*llx",
                  (request.write ? (int)request.wid : -1), (request.write ? "store" : "load"),
                  (int)(sizeof(MemAddr)*2), (unsigned long long)request.address);

    m_outgoing.Pop();
    return SUCCESS;
}

void DCache::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Data Cache stores data from memory that has been used in loads and stores\n"
    "for faster access. Compared to a traditional cache, this D-Cache is extended\n"
    "with several fields to support the multiple threads and asynchronous operation.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Display global information such as hit-rate and configuration.\n"
    "- inspect <component> buffers\n"
    "  Reads and display the outgoing request buffer.\n"
    "- inspect <component> lines\n"
    "  Reads and displays the cache-lines.\n";
}

void DCache::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    auto& regFile = GetDRISC().GetRegisterFile();

    if (arguments.empty())
    {
        out << "Cache type:          ";
        if (m_assoc == 1) {
            out << "Direct mapped" << endl;
        } else if (m_assoc == m_lines.size()) {
            out << "Fully associative" << endl;
        } else {
            out << dec << m_assoc << "-way set associative" << endl;
        }

        out << "L1 bank mapping:     " << m_selector->GetName() << endl
            << "Cache size:          " << dec << (m_lineSize * m_lines.size()) << " bytes" << endl
            << "Cache line size:     " << dec << m_lineSize << " bytes" << endl
            << endl;

        uint64_t numRAccesses = m_numRHits + m_numDelayedReads;

        uint64_t numRRqst = m_numEmptyRMisses + m_numResolvedConflicts;
        uint64_t numWRqst = m_numWAccesses;
        uint64_t numRqst = numRRqst + numWRqst;

        uint64_t numRStalls = m_numHardConflicts + m_numInvalidRMisses + m_numStallingRMisses;
        uint64_t numWStalls = m_numLoadingWMisses + m_numStallingWMisses;
        uint64_t numStalls = numRStalls + numWStalls;

        if (numRAccesses == 0 && m_numWAccesses == 0 && numStalls == 0)
            out << "No accesses so far, cannot provide statistical data." << endl;
        else
        {
            out << "***********************************************************" << endl
                << "                      Summary                              " << endl
                << "***********************************************************" << endl
                << endl << dec
                << "Number of read requests from client:  " << numRAccesses << endl
                << "Number of write requests from client: " << m_numWAccesses << endl
                << "Number of requests to upstream:       " << numRqst      << endl
                << "Number of snoops from siblings:       " << m_numSnoops  << endl
                << "Number of stalled cycles:             " << numStalls    << endl
                << endl;

#define PRINTVAL(X, q) dec << (X) << " (" << setprecision(2) << fixed << (X) * q << "%)"

            float r_factor = 100.0f / numRAccesses;
            out << "***********************************************************" << endl
                << "                      Cache reads                          " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of read requests from client:                " << numRAccesses << endl
                << "Read hits:                                          " << PRINTVAL(m_numRHits, r_factor) << endl
                << "Read misses:                                        " << PRINTVAL(m_numDelayedReads, r_factor) << endl
                << "Breakdown of read misses:" << endl
                << "- to an empty line:                                 " << PRINTVAL(m_numEmptyRMisses, r_factor) << endl
                << "- to a loading line with same tag:                  " << PRINTVAL(m_numLoadingRMisses, r_factor) << endl
                << "- to a reusable line with different tag (conflict): " << PRINTVAL(m_numResolvedConflicts, r_factor) << endl
                << "(percentages relative to " << numRAccesses << " read requests)" << endl
                << endl;

            float w_factor = 100.0f / m_numWAccesses;
            out << "***********************************************************" << endl
                << "                      Cache writes                         " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of write requests from client:                           " << m_numWAccesses << endl
                << "Breakdown of writes:" << endl
                << "- to a loaded line with same tag:                               " << PRINTVAL(m_numWHits, w_factor) << endl
                << "- to a an empty line or line with different tag (pass-through): " << PRINTVAL(m_numPassThroughWMisses, w_factor) << endl
                << "(percentages relative to " << m_numWAccesses << " write requests)" << endl
                << endl;

            float q_factor = 100.0f / numRqst;
            out << "***********************************************************" << endl
                << "                      Requests to upstream                 " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of requests to upstream: " << numRqst  << endl
                << "Read requests:                  " << PRINTVAL(numRRqst, q_factor) << endl
                << "Write requests:                 " << PRINTVAL(numWRqst, q_factor) << endl
                << "(percentages relative to " << numRqst << " requests)" << endl
                << endl;


            if (numStalls != 0)
            {
                float s_factor = 100.f / numStalls;
                out << "***********************************************************" << endl
                    << "                      Stall cycles                         " << endl
                    << "***********************************************************" << endl
                    << endl
                    << "Number of stall cycles:               " << numStalls << endl
                    << "Read-related stalls:                  " << PRINTVAL(numRStalls, s_factor) << endl
                    << "Write-related stalls:                 " << PRINTVAL(numWStalls, s_factor) << endl
                    << "Breakdown of read-related stalls:" << endl
                    << "- read conflict to non-reusable line: " << PRINTVAL(m_numHardConflicts, s_factor) << endl
                    << "- read to invalidated line:           " << PRINTVAL(m_numInvalidRMisses, s_factor) << endl
                    << "- unable to send request upstream:    " << PRINTVAL(m_numStallingRMisses, s_factor) << endl
                    << "Breakdown of write-related stalls:" << endl
                    << "- writes to loading line:             " << PRINTVAL(m_numLoadingWMisses, s_factor) << endl
                    << "- unable to send request upstream:    " << PRINTVAL(m_numStallingWMisses, s_factor) << endl
                    << "(percentages relative to " << numStalls << " stall cycles)" << endl
                    << endl;
            }

        }
        return;
    }
    else if (arguments[0] == "buffers")
    {
        out << endl << "Read responses (CIDs):";
        for (auto& p : m_read_responses)
            out << ' ' << p.cid;
        out << "." << endl

            << endl << "Write responses (TIDs):";
        for (auto& p : m_write_responses)
            out << ' ' << p.wid;
        out << "." << endl

            << endl << "Writeback requests:" << endl;
        for (auto& p : m_writebacks)
        {
            out << "Data: " << hex << setfill('0');
;
            for (size_t x = 0; x < m_lineSize; ++x) {
                if (x && x % sizeof(Integer) == 0) out << ' ';
                out << setw(2) << (unsigned)(unsigned char)p.data[x];
            }
            out << dec << endl
                << "Waiting registers: ";
            RegAddr reg = p.waiting;
            while (reg != INVALID_REG)
            {
                RegValue value;
                regFile.ReadRegister(reg, value, true);

                out << ' ' << reg.str() << " (" << value.str(reg.type) << ')';

                if (value.m_state == RST_FULL || value.m_memory.size == 0)
                {
                    // Rare case: the request info is still in the pipeline, stall!
                    out << " !!";
                    break;
                }

                if (value.m_state != RST_PENDING && value.m_state != RST_WAITING)
                {
                    // We're too fast, wait!
                    out << " !!";
                    break;
                }
                reg = value.m_memory.next;
            }
            out << endl << endl;
        }
        out << endl << "Writeback state: value "
            << hex << showbase << m_wbstate.value << dec << noshowbase
            << " addr " << m_wbstate.addr.str()
            << " next " << m_wbstate.next.str()
            << " size " << m_wbstate.size
            << " offset " << m_wbstate.offset
            << " fid " << m_wbstate.fid
            << endl
            << endl << "Outgoing requests:" << endl
            << "      Address      | Type  | Value (writes)" << endl
            << "-------------------+-------+-------------------------" << endl;
        for (auto &p : m_outgoing)
        {
            out << hex << "0x" << setw(16) << setfill('0') << p.address << " | "
                << (p.write ? "Write" : "Read ") << " |";
            if (p.write)
            {
                out << hex << setfill('0');
                for (size_t x = 0; x < m_lineSize; ++x)
                {
                    if (p.data.mask[x])
                        out << " " << setw(2) << (unsigned)(unsigned char)p.data.data[x];
                    else
                        out << " --";
                }
            }
            out << dec << endl;
        }
        return;
    }

    out << "Set |       Address       |                       Data                      | Waiting Registers" << endl;
    out << "----+---------------------+-------------------------------------------------+-------------------" << endl;
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        const size_t set = i / m_assoc;
        const Line& line = m_lines[i];

        if (i % m_assoc == 0) {
            out << setw(3) << setfill(' ') << dec << right << set;
        } else {
            out << "   ";
        }

        if (line.state == LINE_EMPTY) {
            out << " |                     |                                                 |";
        } else {
            out << " | "
                << hex << "0x" << setw(16) << setfill('0') << m_selector->Unmap(line.tag, set) * m_lineSize;

            switch (line.state)
            {
                case LINE_LOADING: out << "L"; break;
                case LINE_INVALID: out << "I"; break;
                default: out << " ";
            }
            out << " |";

            // Get the waiting registers
            std::vector<RegAddr> waiting;
            RegAddr reg = line.waiting;
            while (reg != INVALID_REG)
            {
                waiting.push_back(reg);
                RegValue value;
                regFile.ReadRegister(reg, value, true);

                if (value.m_state == RST_FULL || value.m_memory.size == 0)
                {
                    // Rare case: the request info is still in the pipeline, stall!
                    waiting.push_back(INVALID_REG);
                    break;
                }

                if (value.m_state != RST_PENDING && value.m_state != RST_WAITING)
                {
                    // We're too fast, wait!
                    waiting.push_back(INVALID_REG);
                    break;
                }

                reg = value.m_memory.next;
            }

            // Print the data
            out << hex << setfill('0');
            static const int BYTES_PER_LINE = 16;
            const int nLines = (m_lineSize + BYTES_PER_LINE + 1) / BYTES_PER_LINE;
            const int nWaitingPerLine = (waiting.size() + nLines + 1) / nLines;
            for (size_t y = 0; y < m_lineSize; y += BYTES_PER_LINE)
            {
                for (size_t x = y; x < y + BYTES_PER_LINE; ++x) {
                    out << " ";
                    if (line.valid[x]) {
                        out << setw(2) << (unsigned)(unsigned char)line.data[x];
                    } else {
                        out << "  ";
                    }
                }

                out << " |";

                // Print waiting registers for this line
                for (int w = 0; w < nWaitingPerLine; ++w)
                {
                    size_t index = y / BYTES_PER_LINE * nWaitingPerLine + w;
                    if (index < waiting.size())
                    {
                        RegAddr wreg = waiting[index];
                        out << " ";
                        if (wreg == INVALID_REG) out << "[...]"; // And possibly others
                        else out << wreg.str();
                    }
                }

                if (y + BYTES_PER_LINE < m_lineSize) {
                    // This was not yet the last line
                    out << endl << "    |                     |";
                }
            }
        }
        out << endl;
        out << ((i + 1) % m_assoc == 0 ? "----" : "    ");
        out << "+---------------------+-------------------------------------------------+-------------------" << endl;
    }

}

}
}
