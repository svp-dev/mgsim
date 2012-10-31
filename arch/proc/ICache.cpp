#include "Processor.h"
#include <sim/log2.h>
#include <sim/config.h>
#include <sim/sampling.h>

#include <cassert>
#include <cstring>
#include <iomanip>
#include <iostream>
using namespace std;

namespace Simulator
{

Processor::ICache::ICache(const std::string& name, Processor& parent, Clock& clock, Allocator& alloc, IMemory& memory, Config& config)
:   Object(name, parent, clock),
    m_parent(parent), m_allocator(alloc),
    m_memory(memory),
    m_selector(IBankSelector::makeSelector(*this, config.getValue<string>(*this, "BankSelector"), config.getValue<size_t>(*this, "NumSets"))),
    m_mcid(0),
    m_lines(),
    m_data(),
    m_outgoing("b_outgoing", *this, clock, config.getValue<BufferSize>(*this, "OutgoingBufferSize")),
    m_incoming("b_incoming", *this, clock, config.getValue<BufferSize>(*this, "IncomingBufferSize")),
    m_lineSize(config.getValue<size_t>("CacheLineSize")),
    m_assoc   (config.getValue<size_t>(*this, "Associativity")),

    m_numHits        (0),
    m_numDelayedReads(0),
    m_numEmptyMisses (0),
    m_numLoadingMisses(0),
    m_numInvalidMisses(0),
    m_numHardConflicts(0),
    m_numResolvedConflicts(0),
    m_numStallingMisses(0),

    p_Outgoing(*this, "outgoing", delegate::create<ICache, &Processor::ICache::DoOutgoing>(*this)),
    p_Incoming(*this, "incoming", delegate::create<ICache, &Processor::ICache::DoIncoming>(*this)),
    p_service(*this, clock, "p_service")
{
    RegisterSampleVariableInObject(m_numHits, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numEmptyMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numLoadingMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numInvalidMisses, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numHardConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numResolvedConflicts, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_numStallingMisses, SVC_CUMULATIVE);

    config.registerObject(m_parent, "cpu");

    StorageTraceSet traces;
    m_mcid = m_memory.RegisterClient(*this, p_Outgoing, traces, m_incoming);
    p_Outgoing.SetStorageTraces(traces);

    m_outgoing.Sensitive( p_Outgoing );
    m_incoming.Sensitive( p_Incoming );

    // These things must be powers of two
    if (!IsPowerOfTwo(m_assoc))
    {
        throw exceptf<InvalidArgumentException>(*this, "Associativity = %zd is not a power of two", m_assoc);
    }

    const size_t sets = m_selector->GetNumBanks();
    if (!IsPowerOfTwo(sets))
    {
        throw exceptf<InvalidArgumentException>(*this, "NumSets = %zd is not a power of two", sets);
    }

    if (!IsPowerOfTwo(m_lineSize))
    {
        throw exceptf<InvalidArgumentException>(*this, "CacheLineSize = %zd is not a power of two", m_lineSize);
    }

    // At least a complete register value has to fit in a line
    if (m_lineSize < sizeof(Integer))
    {
        throw exceptf<InvalidArgumentException>(*this, "CacheLineSize = %zd cannot be less than a word.", m_lineSize);
    }

    // Initialize the cache lines
    m_lines.resize(sets * m_assoc);
    m_data.resize(m_lineSize * m_lines.size());
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        Line& line = m_lines[i];
        line.state        = LINE_EMPTY;
        line.data         = &m_data[i * m_lineSize];
        line.references   = 0;
        line.waiting.head = INVALID_TID;
        line.creation     = false;
    }
}

Processor::ICache::~ICache()
{
    delete m_selector;
}

bool Processor::ICache::IsEmpty() const
{
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        if (m_lines[i].state != LINE_EMPTY && m_lines[i].references != 0)
        {
            return false;
        }
    }
    return true;
}

//
// Finds the line for the specified address. Returns:
// SUCCESS - Line found (hit)
// DELAYED - Line not found (miss), but empty one allocated
// FAILED  - Line not found (miss), no empty lines to allocate
//
Result Processor::ICache::FindLine(MemAddr address, Line* &line, bool check_only)
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

        if (line->state == LINE_EMPTY)
        {
            // Empty line, remember this one
            empty = line;
        }
        else if (line->tag == tag)
        {
            // The wanted line was in the cache
            return SUCCESS;
        }
        else if (line->references == 0 && (replace == NULL || line->access < replace->access))
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
        DeadlockWrite("Unable to allocate a cache-line for the request to %#016llx (set %u)",
            (unsigned long long)address, (unsigned)(set / m_assoc));
        return FAILED;
    }

    if (!check_only)
    {
        COMMIT
        {
            // Reset the line
            line->tag = tag;
        }
    }
    return DELAYED;
}

bool Processor::ICache::ReleaseCacheLine(CID cid)
{
    if (cid != INVALID_CID)
    {
        // Once the references hit zero, the line can be replaced by a next request
        COMMIT
        {
            Line& line = m_lines[cid];
            assert(line.references > 0);
            if (--line.references == 0 && line.state == LINE_INVALID)
            {
                line.state = LINE_EMPTY;
            }
        }
    }
    return true;
}

bool Processor::ICache::Read(CID cid, MemAddr address, void* data, MemSize size) const
{
    MemAddr tag;
    size_t unused;
    m_selector->Map(address / m_lineSize, tag, unused);
    size_t  offset = (size_t)(address % m_lineSize);

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

    if (m_lines[cid].state == LINE_EMPTY || m_lines[cid].tag != tag)
    {
        throw exceptf<InvalidArgumentException>(*this, "Read (%#016llx, %zd): Attempting to read from an invalid cache line",
                                                (unsigned long long)address, (size_t)size);
    }

    const Line& line = m_lines[cid];

    // Verify that we're actually reading a fetched line
    assert(line.state == LINE_FULL || line.state == LINE_INVALID);

    COMMIT{ memcpy(data, line.data + offset, (size_t)size); }
    return true;
}

// For family creation
Result Processor::ICache::Fetch(MemAddr address, MemSize size, CID& cid)
{
    return Fetch(address, size, NULL, &cid);
}

// For thread activation
Result Processor::ICache::Fetch(MemAddr address, MemSize size, TID& tid, CID& cid)
{
    return Fetch(address, size, &tid, &cid);
}

Result Processor::ICache::Fetch(MemAddr address, MemSize size, TID* tid, CID* cid)
{
    // Check that we're fetching executable memory
    if (!m_parent.CheckPermissions(address, size, IMemory::PERM_EXECUTE))
    {
        throw exceptf<SecurityException>(*this, "Fetch (%#016llx, %zd): Attempting to execute from non-executable memory",
                                         (unsigned long long)address, (size_t)size);
    }

    // Validate input
    size_t offset = (size_t)(address % m_lineSize);
    if (offset + size > m_lineSize)
    {
        throw exceptf<InvalidArgumentException>(*this, "Fetch (%#016llx, %zd): Address range crosses over cache line boundary",
                                                (unsigned long long)address, (size_t)size);
    }

#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw exceptf<InvalidArgumentException>(*this, "Fetch (%#016llx, %zd): Size argument too big",
                                                (unsigned long long)address, (size_t)size);
    }
#endif

    if (!p_service.Invoke())
    {
        return FAILED;
    }

    // Align the address
    address = address - offset;

    // Check the cache
    Line*   line;
    Result  result;
    if ((result = FindLine(address, line)) == FAILED)
    {
        // No cache lines are available
        // DeadlockWrite was already called in FindLine (which has more information)
        ++m_numHardConflicts;
        return FAILED;
    }

    // Update access time
    COMMIT{ line->access = GetCycleNo(); }

    // If the caller wants the line index, give it
    if (cid != NULL)
    {
        *cid = line - &m_lines[0];
    }

    if (result == SUCCESS)
    {
        // Cache hit
        if (line->state == LINE_INVALID)
        {
            // This line has been invalidated, we have to wait until it's cleared
            // so we can request a new one
            ++m_numInvalidMisses;
            return FAILED;
        }

        // Update reference count
        COMMIT{ line->references++; }

        if (line->state == LINE_FULL)
        {
            // The line was already fetched so we're done.
            // This is 'true' hit in that we don't have to wait.
            COMMIT{ ++m_numHits; }
            return SUCCESS;
        }

        // The line is being fetched
        assert(line->state == LINE_LOADING);
        COMMIT
        {
            if (tid != NULL)
            {
                // Add the thread to the queue
                TID head = line->waiting.head;
                if (line->waiting.head == INVALID_TID) {
                    line->waiting.tail = *tid;
                }
                line->waiting.head = *tid;
                *tid = head;
            }
            else if (cid != NULL)
            {
                // Initial line for creation
                assert(!line->creation);
                line->creation = true;
            }

            // Statistics
            ++m_numLoadingMisses;
        }
    }
    else
    {
        // Cache miss; a line has been allocated, fetch the data
        if (!m_outgoing.Push(address))
        {
            DeadlockWrite("Unable to put request for I-Cache line into outgoing buffer");
            ++m_numStallingMisses;
            return FAILED;
        }

        // Data is being fetched
        COMMIT
        {
            // Statistics
            if (line->state == LINE_EMPTY)
                ++m_numEmptyMisses;
            else
                ++m_numResolvedConflicts;

            // Initialize buffer
            line->creation   = false;
            line->references = 1;
            line->state      = LINE_LOADING;

            if (tid != NULL)
            {
                // Initialize the waiting queue
                line->waiting.head = *tid;
                line->waiting.tail = *tid;
                *tid = INVALID_TID;
            }
            else if (cid != NULL)
            {
                // Line is used in family creation
                line->creation = true;
            }
        }
    }

    COMMIT{ ++m_numDelayedReads; }

    return DELAYED;
}

bool Processor::ICache::OnMemoryReadCompleted(MemAddr addr, const char *data)
{
    // Instruction cache line returned, store in cache and Buffer

    // Find the line
    Line* line;
    if (FindLine(addr, line, true) == SUCCESS && line->state != LINE_FULL)
    {
        // We need this data, store it
        assert(line->state == LINE_LOADING || line->state == LINE_INVALID);

        COMMIT
        {
            std::copy(data, data + m_lineSize, line->data);
        }

        CID cid = line - &m_lines[0];
        if (!m_incoming.Push(cid))
        {
            DeadlockWrite("Unable to buffer I-Cache line read completion for line #%u", (unsigned)cid);
            return false;
        }
    }
    return true;
}

bool Processor::ICache::OnMemoryWriteCompleted(TID /*tid*/)
{
    // The I-Cache never writes
    assert(false);
    return false;
}

bool Processor::ICache::OnMemorySnooped(MemAddr address, const char * data, const bool * mask)
{
    Line* line;
    // Cache coherency: check if we have the same address
    if (FindLine(address, line, true) == SUCCESS)
    {
        // We do, update the data
        COMMIT{
            line::blit(line->data, data, mask, m_lineSize);
        }
    }
    return true;
}

bool Processor::ICache::OnMemoryInvalidated(MemAddr address)
{
    COMMIT
    {
        Line* line;
        if (FindLine(address, line, true) == SUCCESS)
        {
            if (line->state == LINE_FULL) {
                // Valid lines without references are invalidated by clearing then. Simple.
                // Otherwise, we invalidate them.
                line->state = (line->references == 0) ? LINE_EMPTY : LINE_INVALID;
            } else if (line->state != LINE_INVALID) {
                // Mark the line as invalidated. After it has been loaded and used it will be cleared
                assert(line->state == LINE_LOADING);
                line->state = LINE_INVALID;
            }
        }
    }
    return true;
}

Object& Processor::ICache::GetMemoryPeer()
{
    return m_parent;
}

Result Processor::ICache::DoOutgoing()
{
    assert(!m_outgoing.Empty());
    const MemAddr& address = m_outgoing.Front();
    if (!m_memory.Read(m_mcid, address))
    {
        // The fetch failed
        DeadlockWrite("Unable to read %#016llx from memory", (unsigned long long)address);
        return FAILED;
    }
    m_outgoing.Pop();
    return SUCCESS;
}

Result Processor::ICache::DoIncoming()
{
    assert(!m_incoming.Empty());

    if (!p_service.Invoke())
    {
        return FAILED;
    }

    CID   cid  = m_incoming.Front();
    Line& line = m_lines[cid];
    COMMIT{ line.state = LINE_FULL; }

    if (line.creation)
    {
        // Resume family creation
        if (!m_allocator.OnICachelineLoaded(cid))
        {
            DeadlockWrite("Unable to resume family creation for C%u", (unsigned)cid);
            return FAILED;
        }
        COMMIT{ line.creation = false; }
    }

    if (line.waiting.head != INVALID_TID)
    {
        // Reschedule the line's waiting list
        if (!m_allocator.QueueActiveThreads(line.waiting))
        {
            DeadlockWrite("Unable to queue active threads T%u through T%u for C%u",
                (unsigned)line.waiting.head, (unsigned)line.waiting.tail, (unsigned)cid);
            return FAILED;
        }

        // Clear the waiting list
        COMMIT
        {
            line.waiting.head = INVALID_TID;
            line.waiting.tail = INVALID_TID;
           }
    }
    m_incoming.Pop();
    return SUCCESS;
}

void Processor::ICache::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Instruction Cache stores data from memory that contains instructions for\n"
    "active threads for faster access. Compared to a traditional cache, this I-Cache\n"
    "is extended with several fields to support the multiple threads.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the cache-lines, and global information such as hit-rate\n"
    "  and cache configuration.\n";
}

void Processor::ICache::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
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

        uint64_t numRAccesses = m_numHits + m_numDelayedReads;
        uint64_t numRqst = m_numEmptyMisses + m_numResolvedConflicts;
        uint64_t numStalls = m_numHardConflicts + m_numInvalidMisses + m_numStallingMisses;

#define PRINTVAL(X, q) dec << (X) << " (" << setprecision(2) << fixed << (X) * q << "%)"

        if (numRAccesses == 0)
            out << "No accesses so far, cannot provide statistical data." << endl;
        else
        {
            out << "***********************************************************" << endl
                << "                      Summary                              " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of read requests from client: " << numRAccesses << endl
                << "Number of request to upstream:       " << numRqst << endl
                << "Number of stall cycles:              " << numStalls << endl
                << endl << endl;

            float r_factor = 100.0f / numRAccesses;
            out << "***********************************************************" << endl
                << "                      Cache reads                          " << endl
                << "***********************************************************" << endl
                << endl
                << "Number of read requests from client:                " << numRAccesses << endl
                << "Read hits:                                          " << PRINTVAL(m_numHits, r_factor) << endl
                << "Read misses:                                        " << PRINTVAL(m_numDelayedReads, r_factor) << endl
                << "Breakdown of read misses:" << endl
                << "- to an empty line:                                 " << PRINTVAL(m_numEmptyMisses, r_factor) << endl
                << "- to a loading line with same tag:                  " << PRINTVAL(m_numLoadingMisses, r_factor) << endl
                << "- to a reusable line with different tag (conflict): " << PRINTVAL(m_numResolvedConflicts, r_factor) << endl
                << "(percentages relative to " << numRAccesses << " read requests)" << endl
                << endl;

            if (numStalls != 0)
            {
                float s_factor = 100.f / numStalls;
                out << "***********************************************************" << endl
                    << "                      Stall cycles                         " << endl
                    << "***********************************************************" << endl
                    << endl
                    << "Number of stall cycles:               " << numStalls << endl
                    << "Breakdown:" << endl
                    << "- read conflict to non-reusable line: " << PRINTVAL(m_numHardConflicts, s_factor) << endl
                    << "- read to invalidated line:           " << PRINTVAL(m_numInvalidMisses, s_factor) << endl
                    << "- unable to send request upstream:    " << PRINTVAL(m_numStallingMisses, s_factor) << endl
                    << "(percentages relative to " << numStalls << " stall cycles)" << endl
                    << endl;
            }


        }
        return;
    }
    else if (arguments[0] == "buffers")
    {
        out << endl << "Outgoing buffer:" << endl;
        if (m_outgoing.Empty()) {
            out << "(Empty)" << endl;
        } else {
            out << "     Address     " << endl;
            out << "-----------------" << endl;
            for (Buffer<MemAddr>::const_iterator p = m_outgoing.begin(); p != m_outgoing.end(); ++p)
            {
                out << setfill('0') << hex << setw(16) << *p << endl;
            }
        }

        out << endl << "Incoming buffer:";
        if (m_incoming.Empty()) {
            out << " (Empty)";
        }
        else for (Buffer<CID>::const_iterator p = m_incoming.begin(); p != m_incoming.end(); ++p)
             {
                 out << " C" << dec << *p;
             }
        out << endl;
    }

    out << "Set |       Address       |                       Data                      | Ref |" << endl;
    out << "----+---------------------+-------------------------------------------------+-----+" << endl;
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
            out << " |                     |                                                 |     |";
        } else {
            char state = ' ';
            switch (line.state) {
                case LINE_LOADING: state = 'L'; break;
                case LINE_INVALID: state = 'I'; break;
                default: state = ' '; break;
            }

            out << " | "
                << hex << "0x" << setw(16) << setfill('0') << m_selector->Unmap(line.tag, set) * m_lineSize
                << state << " |";

            if (line.state == LINE_FULL)
            {
                // Print the data
                static const int BYTES_PER_LINE = 16;
                for (size_t y = 0; y < m_lineSize; y += BYTES_PER_LINE)
                {
                    out << hex << setfill('0');
                    for (size_t x = y; x < y + BYTES_PER_LINE; ++x) {
                        out << " " << setw(2) << (unsigned)(unsigned char)line.data[x];
                    }

                    out << " | ";
                    if (y == 0) {
                        out << setw(3) << dec << setfill(' ') << line.references;
                    } else {
                        out << "   ";
                    }
                    out << " |";

                    if (y + BYTES_PER_LINE < m_lineSize) {
                        // This was not yet the last line
                        out << endl << "    |                     |";
                    }
                }
            }
            else
            {
                out << "                                                 | "
                    << setw(3) << dec << setfill(' ') << line.references << " |";
            }
        }
        out << endl;
        out << ((i + 1) % m_assoc == 0 ? "----" : "    ");
        out << "+---------------------+-------------------------------------------------+-----+" << endl;
    }

}

}
