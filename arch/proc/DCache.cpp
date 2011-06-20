#include "Processor.h"
#include "sim/config.h"
#include "sim/sampling.h"

#include <cassert>
#include <cstring>
#include <iomanip>
#include <cstdio>
using namespace std;

namespace Simulator
{

template <typename T>
static bool IsPowerOfTwo(const T& x)
{
    return (x & (x - 1)) == 0;
}

Processor::DCache::DCache(const std::string& name, Processor& parent, Clock& clock, Allocator& alloc, FamilyTable& familyTable, RegisterFile& regFile, IMemory& memory, Config& config)
:   Object(name, parent, clock), m_parent(parent),
    m_allocator(alloc), m_familyTable(familyTable), m_regFile(regFile),
    m_memory(memory),

    m_assoc          (config.getValue<size_t>(*this, "Associativity")),
    m_sets           (config.getValue<size_t>(*this, "NumSets")),
    m_lineSize       (config.getValue<size_t>("CacheLineSize")),
    m_completed      ("b_completed", *this, clock, m_sets * m_assoc),
    m_incoming       ("b_incoming",  *this, clock, config.getValue<BufferSize>(*this, "IncomingBufferSize")),
    m_outgoing       ("b_outgoing",  *this, clock, config.getValue<BufferSize>(*this, "OutgoingBufferSize")),
    m_numHits        (0),
    m_numMisses      (0),

    p_CompletedReads(*this, "completed-reads", delegate::create<DCache, &Processor::DCache::DoCompletedReads   >(*this) ),
    p_Incoming      (*this, "incoming",        delegate::create<DCache, &Processor::DCache::DoIncomingResponses>(*this) ),
    p_Outgoing      (*this, "outgoing",        delegate::create<DCache, &Processor::DCache::DoOutgoingRequests >(*this) ),

    p_service        (*this, clock, "p_service")
{
    RegisterSampleVariableInObject(m_numHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numMisses, SVC_CUMULATIVE);
    
    StorageTraceSet traces;
    m_mcid = m_memory.RegisterClient(*this, p_Outgoing, traces, m_incoming, true);
    p_Outgoing.SetStorageTraces(traces);

    m_completed.Sensitive(p_CompletedReads);
    m_incoming.Sensitive(p_Incoming);
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
        m_lines[i].state = LINE_EMPTY;
        m_lines[i].data  = new char[m_lineSize];
        m_lines[i].valid = new bool[m_lineSize];
    }
    
    m_wbstate.size   = 0;
    m_wbstate.offset = 0;
}

Processor::DCache::~DCache()
{
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        delete[] m_lines[i].data;
        delete[] m_lines[i].valid;
    }
}

Result Processor::DCache::FindLine(MemAddr address, Line* &line, bool check_only)
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

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

Result Processor::DCache::Read(MemAddr address, void* data, MemSize size, LFID /* fid */, RegAddr* reg)
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
    if (!m_parent.CheckPermissions(address, size, IMemory::PERM_READ))
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
        request.data.size = m_lineSize;
        if (!m_outgoing.Push(request))
        {
            DeadlockWrite("Unable to push request to outgoing buffer");
            return FAILED;
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
                m_numHits++;
            }
            return SUCCESS;
        }
        
        // Data is not entirely in the cache; it should be loading from memory
        if (line->state != LINE_LOADING)
        {
            assert(line->state == LINE_INVALID);
            return FAILED;
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
        m_numMisses++;
    }
    return DELAYED;
}

Result Processor::DCache::Write(MemAddr address, void* data, MemSize size, LFID fid, TID tid)
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
    if (!m_parent.CheckPermissions(address, size, IMemory::PERM_WRITE))
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
    
    Line* line;
    if (FindLine(address, line, true) == SUCCESS)
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
            DeadlockWrite("Unable to write into loading cache line");
            return FAILED;
        }
        else
        {
            // Update the line
            assert(line->state == LINE_FULL);
            COMMIT{ memcpy(line->data + offset, data, (size_t)size); }
        }
    }
    
    // Store request for memory (pass-through)
    Request request;
    request.write     = true;
    request.address   = address;
    memcpy(request.data.data, data, size);
    request.data.size = size;
    request.tid       = tid;
    if (!m_outgoing.Push(request))
    {
        DeadlockWrite("Unable to push request to outgoing buffer");
        return FAILED;
    }
    return DELAYED;
}

bool Processor::DCache::OnMemoryReadCompleted(MemAddr addr, const MemData& data)
{
    assert(data.size == m_lineSize);
    
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
            // Copy the data into the cache line.
            // Mask by valid bytes (don't overwrite already written data).
            for (size_t i = 0; i < (size_t)data.size; ++i)
            {
                if (!line->valid[i]) {
                    line->data[i]  = data.data[i];
                     line->valid[i] = true;
                }
            }
            
            line->processing = true;
        }
    
        // Push the cache-line to the back of the queue
        Response response;
        response.write = false;
        response.cid   = line - &m_lines[0];
        if (!m_incoming.Push(response))
        {
            DeadlockWrite("Unable to push read completion to buffer");
            return false;
        }
    }
    return true;
}

bool Processor::DCache::OnMemoryWriteCompleted(TID tid)
{
    // Data has been written
    if (tid != INVALID_TID) // otherwise for DCA
    {
        Response response;
        response.write = true;
        response.tid   = tid;
        if (!m_incoming.Push(response))
        {
            DeadlockWrite("Unable to push write completion to buffer");
            return false;
        }
    }
    return true;
}

bool Processor::DCache::OnMemorySnooped(MemAddr address, const MemData& data)
{
    COMMIT
    {
        size_t offset = (size_t)(address % m_lineSize);
        Line*  line;

        // Cache coherency: check if we have the same address
        if (FindLine(address, line, true) == SUCCESS)
        {
            // We do, update the data
            memcpy(line->data + offset, data.data, (size_t)data.size);
            
            // Mark written bytes as valid
            // Note that we don't have to check against already written data or queued reads
            // because we don't have to guarantee sequential semantics from other cores.
            // This falls within the non-determinism behavior of the architecture.
            std::fill(line->valid + offset, line->valid + offset + data.size, true);
        }
    }
    return true;
}

bool Processor::DCache::OnMemoryInvalidated(MemAddr address)
{
    COMMIT
    {
        Line* line;
        if (FindLine(address, line, true) == SUCCESS)
        {
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

Result Processor::DCache::DoCompletedReads()
{
    assert(!m_completed.Empty());

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for D-Cache access in read completion");
        return FAILED;
    }

    // Process a waiting register
    Line& line = m_lines[m_completed.Front()];
    assert(line.state == LINE_LOADING || line.state == LINE_INVALID);
    if (line.waiting.valid())
    {
        WritebackState state = m_wbstate;
        if (state.offset == state.size)
        {
            // Starting a new multi-register write
        
            // Write to register
            if (!m_regFile.p_asyncW.Write(line.waiting))
            {
                DeadlockWrite("Unable to acquire port to write back %s", line.waiting.str().c_str());
                return FAILED;
            }

            // Read request information
            RegValue value;
            if (!m_regFile.ReadRegister(line.waiting, value))
            {
                DeadlockWrite("Unable to read register %s", line.waiting.str().c_str());
                return FAILED;
            }
        
            if (value.m_state == RST_FULL || value.m_memory.size == 0)
            {
                // Rare case: the request info is still in the pipeline, stall!
                DeadlockWrite("Register %s is not yet written for read completion", line.waiting.str().c_str());
                return FAILED;
            }

            if (value.m_state != RST_PENDING && value.m_state != RST_WAITING)
            {
                // We're too fast, wait!
                DeadlockWrite("Memory read completed before register %s was cleared", line.waiting.str().c_str());
                return FAILED;
            }

            // Ignore the request if the family has been killed
            state.value = UnserializeRegister(line.waiting.type, &line.data[value.m_memory.offset], value.m_memory.size);

            if (value.m_memory.sign_extend)
            {
                // Sign-extend the value
                assert(value.m_memory.size < sizeof(Integer));
                int shift = (sizeof(state.value) - value.m_memory.size) * 8;
                state.value = (int64_t)(state.value << shift) >> shift;
            }

            state.fid    = value.m_memory.fid;
            state.addr   = line.waiting;
            state.next   = value.m_memory.next;
            state.offset = 0;

            // Number of registers that we're writing (must be a power of two)
            state.size = (value.m_memory.size + sizeof(Integer) - 1) / sizeof(Integer);
            assert((state.size & (state.size - 1)) == 0);
        }
        else
        {
            // Write to register
            if (!m_regFile.p_asyncW.Write(state.addr))
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

        DebugMemWrite("Completed load: %#016llx -> %s",
                      (unsigned long long)data, state.addr.str().c_str());

        switch (state.addr.type) {
            case RT_INTEGER: reg.m_integer       = data; break;
            case RT_FLOAT:   reg.m_float.integer = data; break;
        }

        if (!m_regFile.WriteRegister(state.addr, reg, true))
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
            if (!m_allocator.DecreaseFamilyDependency(state.fid, FAMDEP_OUTSTANDING_READS))
            {
                DeadlockWrite("Unable to decrement outstanding reads on F%u", (unsigned)state.fid);
                return FAILED;
            }

            COMMIT{ line.waiting = state.next; }
        }
        COMMIT{ m_wbstate = state; }
    }
    else
    {
        // We're done with this line.
        // Move the line to the FULL (or EMPTY when invalidated) state.
        COMMIT
        {
            line.state = (line.state == LINE_INVALID) ? LINE_EMPTY : LINE_FULL;
        }
        m_completed.Pop();
    }
    return SUCCESS;
}
        
Result Processor::DCache::DoIncomingResponses()
{
    assert(!m_incoming.Empty());
    const Response& response = m_incoming.Front();
    if (response.write)
    {
        if (!m_allocator.DecreaseThreadDependency(response.tid, THREADDEP_OUTSTANDING_WRITES))
        {
            DeadlockWrite("Unable to decrease outstanding writes on T%u", (unsigned)response.tid);
            return FAILED;
        }
    }
    else
    {
        if (!m_completed.Push(response.cid))
        {
            DeadlockWrite("Unable to buffer read completion to processing buffer");
            return FAILED;
        }
    }
    m_incoming.Pop();
    return SUCCESS;
}

Result Processor::DCache::DoOutgoingRequests()
{
    assert(!m_outgoing.Empty());
    const Request& request = m_outgoing.Front();
    if (request.write)
    {
        if (!m_memory.Write(m_mcid, request.address, request.data.data, request.data.size, request.tid))
        {
            DeadlockWrite("Unable to send write of %u bytes to 0x%016llx to memory", (unsigned)request.data.size, (unsigned long long)request.address);
            return FAILED;
        }
    }
    else
    {
        if (!m_memory.Read(m_mcid, request.address, request.data.size))
        {
            DeadlockWrite("Unable to send read of %u bytes to 0x%016llx to memory", (unsigned)request.data.size, (unsigned long long)request.address);
            return FAILED;
        }
    }
    m_outgoing.Pop();
    return SUCCESS;
}

void Processor::DCache::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Data Cache stores data from memory that has been used in loads and stores\n"
    "for faster access. Compared to a traditional cache, this D-Cache is extended\n"
    "with several fields to support the multiple threads and asynchronous operation.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the cache-lines, and global information such as hit-rate\n"
    "  and cache configuration.\n";
}

void Processor::DCache::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out << "Cache type:          ";
    if (m_assoc == 1) {
        out << "Direct mapped" << endl;
    } else if (m_assoc == m_lines.size()) {
        out << "Fully associative" << endl;
    } else {
        out << dec << m_assoc << "-way set associative" << endl;
    }

    out << "Cache size:          " << dec << (m_lineSize * m_lines.size()) << " bytes" << endl;
    out << "Cache line size:     " << dec << m_lineSize << " bytes" << endl;
    out << "Current hit rate:    ";
    if (m_numHits + m_numMisses > 0) {
        out << setprecision(2) << fixed << m_numHits * 100.0f / (m_numHits + m_numMisses) << "%";
    } else {
        out << "N/A";
    }
    out << " (" << dec << m_numHits << " hits, " << m_numMisses << " misses)" << endl;
    out << endl;

    const size_t num_sets = m_lines.size() / m_assoc;

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
                << hex << "0x" << setw(16) << setfill('0') << (line.tag * num_sets + set) * m_lineSize;
            
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
                m_regFile.ReadRegister(reg, value);
                
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
                        RegAddr reg = waiting[index];
                        out << " ";
                        if (reg == INVALID_REG) out << "[...]"; // And possibly others
                        else out << reg.str();
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

    out << endl << "Outgoing requests:" << endl << endl
        << "      Address      | Size | Type  |" << endl
        << "-------------------+------+-------+" << endl;
    for (Buffer<Request>::const_iterator p = m_outgoing.begin(); p != m_outgoing.end(); ++p)
    {
        out << hex << "0x" << setw(16) << setfill('0') << p->address << " | "
            << dec << setw(4) << right << setfill(' ') << p->data.size << " | "
            << (p->write ? "Write" : "Read ") << " | "
            << endl;
    }
}

}
