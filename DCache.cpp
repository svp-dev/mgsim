#include <cassert>
#include "DCache.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

DCache::DCache(Processor& parent, const std::string& name, Allocator& alloc, FamilyTable& familyTable, RegisterFile& regFile, const Config& config)
:   IComponent(&parent, parent.getKernel(), name),
    p_request(parent.getKernel()), m_parent(parent),
	m_allocator(alloc), m_familyTable(familyTable), m_regFile(regFile),
    m_lineSize(config.lineSize), m_assoc(config.assoc), 
    m_numHits(0), m_numMisses(0)
{
    m_returned.head = INVALID_CID;

    // These things must be powers of two
    if (config.assoc == 0 || (config.assoc & ~(config.assoc - 1)) != config.assoc)
    {
        throw InvalidArgumentException("Data cache associativity is not a power of two");
    }

    if (config.sets == 0 || (config.sets & ~(config.sets - 1)) != config.sets)
    {
        throw InvalidArgumentException("Number of sets in data cache is not a power of two");
    }

    if ((m_lineSize & ~(m_lineSize - 1)) != m_lineSize)
    {
        throw InvalidArgumentException("Data cache line size is not a power of two");
    }

    // At least a complete register value has to fit in a line
    if (m_lineSize < 8)
    {
        throw InvalidArgumentException("Data cache line size is less than 8.");
    }

    m_lines.resize(config.sets * config.assoc);
    for (size_t i = 0; i < m_lines.size(); i++)
    {
        m_lines[i].data    = new char[m_lineSize];
        m_lines[i].state   = LINE_INVALID;
		m_lines[i].waiting = INVALID_REG;
    }
    m_numWaiting = 0;
}

DCache::~DCache()
{
    for (size_t i = 0; i < m_lines.size(); i++)
    {
        delete[] m_lines[i].data;
    }
}

Result DCache::findLine(MemAddr address, Line* &line, bool reset)
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
        if (line->state == LINE_INVALID)
        {
			if (line->waiting == INVALID_REG)
			{
				// Empty, unused line, remember this one
				empty = line;
			}
        }
        else if (line->tag == tag)
        {
            // The wanted line was in the cache
            return SUCCESS;
        }
        else if (line->state == LINE_VALID && (replace == NULL || line->access < replace->access))
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

	if (reset)
	{
		// Reset the line
		COMMIT
		{
			line->tag  = tag;
		    line->next = INVALID_CID;
		}
	}

    return DELAYED;
}

Result DCache::read(MemAddr address, void* data, MemSize size, LFID fid, RegAddr* reg)
{
    size_t offset = (size_t)(address % m_lineSize);
    if (offset + size > m_lineSize)
    {
        throw InvalidArgumentException("Address range crosses over cache line boundary");
    }

    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	// Check that we're reading readable memory
	if (!m_parent.checkPermissions(address, size, IMemory::PERM_READ))
	{
		throw SecurityException("Attempting to read non-readable memory");
	}

    Line*  line;
    Result result;
    // SUCCESS - A line with the address was found
    // DELAYED - The line with the address was not found, but a line can be allocated
    // FAILED  - No usable line was found at all
    if ((result = findLine(address - offset, line)) == FAILED)
    {
        // Cache-miss and no free line
        return FAILED;
    }
    else if (result == DELAYED)
    {
        // Fetch the data
        if ((result = m_parent.readMemory(address - offset, line->data, m_lineSize, MemTag(line - &m_lines[0], true))) == FAILED)
        {
            return FAILED;
        }
    }
    else if (line->state == LINE_LOADING || line->state == LINE_PROCESSING)
    {
        // The line is present but is being loaded or processed
        result = DELAYED;
    }

    // Reset last line access
    COMMIT{ line->access  = m_parent.getKernel().getCycleNo(); }

    if (result == SUCCESS)
    {
        // Data was already in the cache or has been loaded immediately, copy it
        COMMIT
        {
            line->state = LINE_VALID;
            memcpy(data, line->data + offset, (size_t)size);
            m_numHits++;
        }
        return SUCCESS;
    }

    // Data is being loaded, add request to the queue
	COMMIT
	{
		RegAddr old   = line->waiting;
		line->waiting = *reg;
		line->state   = LINE_LOADING;
		*reg = old;
		m_numWaiting++;
		m_numMisses++;
	}
    return DELAYED;
}

Result DCache::write(MemAddr address, void* data, MemSize size, LFID fid)
{
	assert(fid != INVALID_LFID);

	size_t offset = (size_t)(address % m_lineSize);
    if (offset + size > m_lineSize)
    {
        throw InvalidArgumentException("Address range crosses over cache line boundary");
    }

    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }

	// Check that we're writing writable memory
	if (!m_parent.checkPermissions(address, size, IMemory::PERM_WRITE))
	{
		throw SecurityException("Attempting to write non-writable memory");
	}

	COMMIT
	{
		Line* line;
		if (findLine(address, line, false) == SUCCESS)
		{
			if (line->state == LINE_VALID) {
				// The line is cached and has no outstanding reads, update it
				memcpy(&line->data[offset], data, (size_t)size);
			} else {
				// The line is present, but not yet loaded or still processing outstanding reads, invalid it
				line->state = LINE_INVALID;
			}
		}
	}

    // Pass-through
    return m_parent.writeMemory(address, data, size, MemTag(fid));
}

bool DCache::onMemoryReadCompleted(const MemData& data)
{
    assert(data.tag.data);
    assert(data.tag.cid != INVALID_CID);

    COMMIT
    {
        // Copy the data into the cache line
        memcpy(m_lines[data.tag.cid].data, data.data, (size_t)data.size);

        // Push the cache-line to the back of the queue
        if (m_returned.head == INVALID_CID) {
            m_returned.head = data.tag.cid;
        } else {
            m_lines[m_returned.tail].next = data.tag.cid;
        }
        m_returned.tail = data.tag.cid;
		
		// It might have been invalidated because of a write, check state
		if (m_lines[data.tag.cid].state == LINE_LOADING) {
			m_lines[data.tag.cid].state = LINE_PROCESSING;
		}
    }

    return true;
}

bool DCache::onMemoryWriteCompleted(const MemTag& tag)
{
    // Data has been written
    if (!m_allocator.decreaseFamilyDependency(tag.fid, FAMDEP_OUTSTANDING_WRITES))
    {
        return false;
    }
    return true;
}

bool DCache::onMemorySnooped(MemAddr address, const MemData& data)
{
    COMMIT
    {
        size_t offset = (size_t)(address % m_lineSize);
		Line*  line;

        // Cache coherency: check if we have the same address
		if (findLine(address, line, false) == SUCCESS)
		{
            if (line->state != LINE_LOADING)
            {
                // Yes, update it
                memcpy(&line->data[offset], data.data, (size_t)data.size);
            }
        }
    }
    return true;
}

Result DCache::onCycleWritePhase(int stateIndex)
{
    if (m_returned.head == INVALID_CID)
    {
		// Nothing to do
		return DELAYED;
	}
    
	// Process a waiting register
    Line& line = m_lines[m_returned.head];
	if (line.waiting != INVALID_REG)
	{
		// Write to register
		if (!m_regFile.p_asyncW.write(*this, line.waiting))
		{
			return FAILED;
		}

		// Read request information
		RegValue value;
		if (!m_regFile.readRegister(line.waiting, value))
		{
			return FAILED;
		}

		if (value.m_state == RST_EMPTY || value.m_state == RST_FULL || value.m_request.size == 0)
		{
			// Rare case: the request info is still in the pipeline, stall a cycle!
			return FAILED;
		}
        
        // Register must be in pending or waiting state
        assert(value.m_state == RST_PENDING || value.m_state == RST_WAITING);

		// Ignore the request if the family has been killed
		const Family& family = m_familyTable[value.m_request.fid];
		if (!family.killed)
		{
			// Write to register file
			RegValue data = UnserializeRegister(line.waiting.type, &line.data[value.m_request.offset], value.m_request.size);
			if (!m_regFile.writeRegister(line.waiting, data, *this))
			{
				return FAILED;
			}
		}

		if (!m_allocator.decreaseFamilyDependency(value.m_request.fid, FAMDEP_OUTSTANDING_READS))
		{
			return FAILED;
		}

		COMMIT{
		    line.waiting = value.m_request.next;
            m_numWaiting--;
        }
	}

	if (line.waiting == INVALID_REG)
	{
		// If not invalidated, move the line from processing to used state
		if (line.state != LINE_INVALID) {
			line.state = LINE_VALID;
		}
		m_returned.head = line.next;
	}
	return SUCCESS;
}

