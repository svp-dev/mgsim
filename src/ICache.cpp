#include "ICache.h"
#include "Processor.h"
#include "config.h"
#include <cassert>
#include <cstring>
#include <iomanip>
using namespace std;

namespace Simulator
{

template <typename T>
static bool IsPowerOfTwo(const T& x)
{
    return (x & (x - 1)) == 0;
}

ICache::ICache(Processor& parent, const std::string& name, Allocator& alloc, const Config& config)
:   IComponent(&parent, parent.GetKernel(), name, "outgoing|incoming"),
    m_parent(parent), m_allocator(alloc),
    m_outgoing(parent.GetKernel(), config.getInteger<BufferSize>("ICacheOutgoingBufferSize", 1)),
    m_incoming(parent.GetKernel(), config.getInteger<BufferSize>("ICacheIncomingBufferSize", 1)),
    m_numHits(0),
    m_numMisses(0),
    m_lineSize(config.getInteger<size_t>("CacheLineSize", 64)),
    m_assoc   (config.getInteger<size_t>("ICacheAssociativity", 4)),
    p_service(*this, "p_service")
{
    m_outgoing.Sensitive(*this, 0);
    m_incoming.Sensitive(*this, 1);

    // These things must be powers of two
    if (!IsPowerOfTwo(m_assoc))
    {
        throw InvalidArgumentException("Instruction cache associativity is not a power of two");
    }

    const size_t sets = config.getInteger<size_t>("ICacheNumSets", 4);
    if (!IsPowerOfTwo(sets))
    {
        throw InvalidArgumentException("Number of sets in instruction cache is not a power of two");
    }

    if (!IsPowerOfTwo(m_lineSize))
    {
        throw InvalidArgumentException("Instruction cache line size is not a power of two");
    }

    // At least a complete register value has to fit in a line
    if (m_lineSize < 8)
    {
        throw InvalidArgumentException("Instruction cache line size cannot be less than 8.");
    }

	// Initialize the cache lines
    m_lines.resize(sets * m_assoc);
	m_data.resize(m_lineSize * m_lines.size());
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        m_lines[i].data         = &m_data[i * m_lineSize];
        m_lines[i].used         = false;
		m_lines[i].references   = 0;
        m_lines[i].waiting.head = INVALID_TID;
		m_lines[i].creation     = false;
		m_lines[i].fetched      = false;
    }
}

ICache::~ICache()
{
}

bool ICache::IsEmpty() const
{
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        if (m_lines[i].used && m_lines[i].references != 0)
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
Result ICache::FindLine(MemAddr address, Line* &line)
{
    size_t  sets = m_lines.size() / m_assoc;
    MemAddr tag  = (address / m_lineSize) / sets;
    size_t  set  = (size_t)((address / m_lineSize) % sets) * m_assoc;

    // Find the line
    Line* empty   = NULL;
    Line* replace = NULL;
    for (size_t i = 0; i < m_assoc; ++i)
    {
        line = &m_lines[set + i];
        if (!line->used)
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
        DeadlockWrite("Unable to allocate a cache-line for the request to 0x%016llx (set %u)",
            (unsigned long long)address, (unsigned)(set / m_assoc));
        return FAILED;
    }

    COMMIT
    {
        // Reset the line
        line->tag = tag;
    }

    return DELAYED;
}

bool ICache::ReleaseCacheLine(CID cid)
{
    if (cid != INVALID_CID)
    {
		// Once the references hit zero, the line can be replaced by a next request
        COMMIT
        {
			assert(m_lines[cid].references > 0);
            m_lines[cid].references--;
        }
    }
    return true;
}

bool ICache::Read(CID cid, MemAddr address, void* data, MemSize size) const
{
    size_t  sets   = m_lines.size() / m_assoc;
    MemAddr tag    = (address / m_lineSize) / sets;
    size_t  offset = (size_t)(address % m_lineSize);

    if (offset + size > m_lineSize)
    {
        throw InvalidArgumentException("Address range crosses over cache line boundary");
    }

#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

    if (!m_lines[cid].used || m_lines[cid].tag != tag)
    {
        throw InvalidArgumentException("Attempting to read from an invalid cache line");
    }

	// Verify that we're actually reading a fetched line
	assert(m_lines[cid].fetched);

    COMMIT{ memcpy(data, m_lines[cid].data + offset, (size_t)size); }
    return true;
}

// For family creation
Result ICache::Fetch(MemAddr address, MemSize size, CID& cid)
{
	return Fetch(address, size, NULL, &cid);
}

// For thread activation
Result ICache::Fetch(MemAddr address, MemSize size, TID& tid, CID& cid)
{
	return Fetch(address, size, &tid, &cid);
}

Result ICache::Fetch(MemAddr address, MemSize size, TID* tid, CID* cid)
{
	// Check that we're fetching executable memory
	if (!m_parent.CheckPermissions(address, size, IMemory::PERM_EXECUTE))
	{
		throw SecurityException("Attempting to execute from non-executable memory", *this);
	}

    // Validate input
    size_t offset = (size_t)(address % m_lineSize);
    if (offset + size > m_lineSize)
    {
        throw InvalidArgumentException("Address range crosses over cache line boundary");
    }

#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
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
        return FAILED;
    }

    // Update access time
	COMMIT{ line->access = m_parent.GetKernel().GetCycleNo(); }

    // If the caller wants the line index, give it
	if (cid != NULL)
	{
		*cid = line - &m_lines[0];
	}

    if (result == SUCCESS)
    {
        // Cache hit

        // Update reference count
	    COMMIT{ line->references++; }
	    
        if (line->fetched)
        {
            // The line was already fetched so we're done.
            // This is 'true' hit in that we don't have to wait.
            COMMIT{ m_numHits++; }
            return SUCCESS;
        }
        
        // The line is being fetched
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
		}
    }
	else
	{
		// Cache miss; a line has been allocated, fetch the data
        Request request;
        request.address = address;
        request.tag     = MemTag(line - &m_lines[0], false);		
		if (!m_outgoing.Push(request))
		{
		    DeadlockWrite("Unable to put request for I-Cache line into outgoing buffer");
		    return FAILED;
		}

		// Data is being fetched
		COMMIT
		{
			// Initialize buffer
			line->creation   = false;
			line->references = 1;
			line->used       = true;

			line->fetched = false;
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

    COMMIT{ m_numMisses++; }
    return DELAYED;
}

bool ICache::OnMemoryReadCompleted(const MemData& data)
{
    // Instruction cache line returned, store in cache and Buffer
    assert(data.size == m_lineSize);
    
    if (!m_incoming.Push(data.tag.cid))
    {
        DeadlockWrite("Unable to buffer I-Cache line read completion for line #%u", (unsigned)data.tag.cid);
        return false;
    }

	Line& line = m_lines[data.tag.cid];
    COMMIT
    {
        memcpy(line.data, data.data, (size_t)data.size);
    }
    return true;
}

Result ICache::OnCycle(unsigned int stateIndex)
{
    switch (stateIndex)
    {
        case 0:
        {
            assert(!m_outgoing.Empty());
            const Request& request = m_outgoing.Front();
		    if (!m_parent.ReadMemory(request.address, m_lineSize, request.tag))
		    {
    			// The fetch failed
			    DeadlockWrite("Unable to read 0x%016llx from memory", (unsigned long long)request.address);
			    return FAILED;
		    }
		    m_outgoing.Pop();
		    return SUCCESS;
        }
        
        case 1:
        {
            assert(!m_incoming.Empty());
	        
            if (!p_service.Invoke())
            {
                return FAILED;
            }
            
            CID   cid  = m_incoming.Front();
	        Line& line = m_lines[cid];            
            COMMIT{ line.fetched = true; }

	        if (line.creation)
	        {
		        // Resume family creation
		        if (!m_allocator.OnCachelineLoaded(cid))
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
    }
    return DELAYED;
}

void ICache::Cmd_Help(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Instruction Cache stores data from memory that contains instructions for\n"
    "active threads for faster access. Compared to a traditional cache, this I-Cache\n"
    "is extended with several fields to support the multiple threads.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the cache-lines, and global information such as hit-rate\n"
    "  and cache configuration.\n";
}

void ICache::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
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
        
        if (!line.used) {
            out << " |                     |                                                 |     |";
        } else {
            out << " | "
                << hex << "0x" << setw(16) << setfill('0') << (line.tag * num_sets + set) * m_lineSize
                << (line.waiting.head != INVALID_TID || line.creation ? "L" : " ")
                << " |";
            
            if (line.waiting.head == INVALID_TID && !line.creation)
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
                out << "                                                 |     |";
            }
        }
        out << endl;
        out << ((i + 1) % m_assoc == 0 ? "----" : "    ");
        out << "+---------------------+-------------------------------------------------+-----+" << endl;
    }
    
    out << endl << "Outgoing buffer:" << endl;
    if (m_outgoing.Empty()) {
        out << "(Empty)" << endl;
    } else {
        out << "     Address     | CID" << endl;
        out << "-----------------+-----" << endl;
        for (Buffer<Request>::const_iterator p = m_outgoing.begin(); p != m_outgoing.end(); ++p)
        {
            out << setfill('0') << hex << setw(16) << p->address << " | "
                << setfill(' ') << dec << setw( 4) << p->tag.cid
                << endl;
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

}
