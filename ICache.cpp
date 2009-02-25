#include <cassert>
#include "ICache.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

ICache::ICache(Processor& parent, const std::string& name, Allocator& alloc, const Config& config)
:   IComponent(&parent, parent.GetKernel(), name),
    p_request(parent.GetKernel()),
    m_parent(parent), m_allocator(alloc),
    m_lineSize(config.lineSize), m_numHits(0), m_numMisses(0), m_assoc(config.assoc)
{
    // These things must be powers of two
    if ((config.assoc & ~(config.assoc - 1)) != config.assoc)
    {
        throw InvalidArgumentException("Instruction cache associativity is not a power of two");
    }

    if ((config.sets & ~(config.sets - 1)) != config.sets)
    {
        throw InvalidArgumentException("Number of sets in instruction cache is not a power of two");
    }

    if ((m_lineSize & ~(m_lineSize - 1)) != m_lineSize)
    {
        throw InvalidArgumentException("Instruction cache line size is not a power of two");
    }

    // At least a complete register value has to fit in a line
    if (m_lineSize < 8)
    {
        throw InvalidArgumentException("Instruction cache line size is less than 8.");
    }

	// Initialize the cache lines
    m_lines.resize(config.sets * config.assoc);
	m_data = new char[m_lineSize * m_lines.size()];
    for (size_t i = 0; i < m_lines.size(); i++)
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
    delete[] m_data;
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
    for (size_t i = 0; i < m_assoc; i++)
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

    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

    if (!m_lines[cid].used || m_lines[cid].tag != tag)
    {
        throw InvalidArgumentException("Attempting to read from an invalid cache line");
    }

	// Verify that we're actually reading a fetched line
	assert(m_lines[cid].fetched);

    COMMIT{ memcpy(data, m_lines[cid].data + offset, size); }
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
	assert(cid != NULL);

	// Check that we're fetching executable memory
	if (!m_parent.CheckPermissions(address, size, IMemory::PERM_EXECUTE))
	{
		throw SecurityException(*this, "Attempting to execute non-executable memory");
	}

    // Validate input
    size_t offset = (size_t)(address % m_lineSize);
    if (offset + size > m_lineSize)
    {
        throw InvalidArgumentException("Address range crosses over cache line boundary");
    }

    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	// Align the address
    address = address - offset;

    // Check the cache
    Line*   line;
    Result  result;
    if ((result = FindLine(address, line)) == FAILED)
    {
        // No cache lines are available
        return FAILED;
    }

    if (result == SUCCESS)
    {
        // Cache hit
        if (!line->fetched)
        {
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
				else
				{
					// Initial line for creation
					assert(!line->creation);
					line->creation = true;
				}
			}
            result = DELAYED;
        }
        COMMIT{ m_numHits++; }
    }
	else
	{
		// The data has to be fetched
		MemData data;
		data.data = line->data;
		data.size = m_lineSize;
		data.tag  = MemTag(line - &m_lines[0], false);

		// Cache miss, fetch the data
		if ((result = m_parent.ReadMemory(address, data.data, data.size, data.tag)) == FAILED)
		{
			// The fetch failed
			return FAILED;
		}
		
		// Data has been fetched or is being fetched
		COMMIT
		{
			// Initialize buffer
			line->creation   = false;
			line->references = 0;
			line->used       = true;

			if (result == SUCCESS)
			{
				// Data was fetched immediately, copy it
				memcpy(line->data, data.data, data.size);
				m_numHits++;
			}
			else
			{
				// Data is being fetched
				line->fetched = false;
				if (tid != NULL)
				{
					// Initialize the waiting queue
					line->waiting.head = *tid;
					line->waiting.tail = *tid;
					*tid = INVALID_TID;
				}
				else
				{
					line->creation = true;
				}
				m_numMisses++;
			}
		}
	}

    // Update line
    COMMIT
	{
		line->access = m_parent.GetKernel().GetCycleNo();
		line->references++;
	}

	if (cid != NULL)
	{
		*cid = line - &m_lines[0];
	}

    // Success or Delayed
    return result;
}

bool ICache::OnMemoryReadCompleted(const MemData& data)
{
    // Instruction cache line returned, store in cache and Buffer
    assert(data.size == m_lineSize);

	Line& line = m_lines[data.tag.cid];
    COMMIT
    {
        memcpy(line.data, data.data, data.size);
		line.fetched = true;
    }

	if (line.creation)
	{
		// Resume family creation
		if (!m_allocator.OnCachelineLoaded(data.tag.cid))
		{
			return false;
		}
		COMMIT{ line.creation = false; }
	}

	if (line.waiting.head != INVALID_TID)
	{
		// Reschedule the line's waiting list
		if (!m_allocator.QueueActiveThreads(line.waiting.head, line.waiting.tail))
		{
			return false;
		}

		// Clear the waiting list
		COMMIT
		{
			line.waiting.head = INVALID_TID;
			line.waiting.tail = INVALID_TID;
		}
	}

    return true;
}
