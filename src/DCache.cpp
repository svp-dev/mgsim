#include "DCache.h"
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

DCache::DCache(Processor& parent, const std::string& name, Allocator& alloc, FamilyTable& familyTable, RegisterFile& regFile, const Config& config)
:   IComponent(&parent, parent.GetKernel(), name), m_parent(parent),
	m_allocator(alloc), m_familyTable(familyTable), m_regFile(regFile),
	m_assoc   (config.getInteger<size_t>("DCacheAssociativity", 4)),
	m_sets    (config.getInteger<size_t>("DCacheNumSets", 4)),
	m_lineSize(config.getInteger<size_t>("CacheLineSize", 64)),
	m_numHits(0),
	m_numMisses(0)
{
    m_returned.head = INVALID_CID;

    // These things must be powers of two
    if (m_assoc == 0 || !IsPowerOfTwo(m_assoc))
    {
        throw InvalidArgumentException("Data cache associativity is not a power of two");
    }

    if (m_sets == 0 || !IsPowerOfTwo(m_sets))
    {
        throw InvalidArgumentException("Number of sets in data cache is not a power of two");
    }

    if (m_lineSize == 0 || !IsPowerOfTwo(m_lineSize))
    {
        throw InvalidArgumentException("Data cache line size is not a power of two");
    }

    // At least a complete register value has to fit in a line
    if (m_lineSize < 8)
    {
        throw InvalidArgumentException("Data cache line size is less than 8.");
    }

    m_lines.resize(m_sets * m_assoc);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        m_lines[i].state = LINE_EMPTY;
        m_lines[i].data  = new char[m_lineSize];
    }
    m_numWaiting = 0;
}

DCache::~DCache()
{
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        delete[] m_lines[i].data;
    }
}

Result DCache::FindLine(MemAddr address, Line* &line, bool check_only)
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    Line* empty   = NULL;
    Line* replace = NULL;
    for (size_t i = 0; i < m_assoc; ++i)
    {
        line = &m_lines[set + i];

        // Invalid lines cannot be touched or considered
        if (line->state != LINE_INVALID)
        {
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
    }
    
    // The line could not be found, allocate the empty line or replace an existing line
    line = (empty != NULL) ? empty : replace;
    if (line == NULL)
    {
        // No available line
        return FAILED;
    }

	if (!check_only)
	{
		// Reset the line
		COMMIT
		{
			line->tag     = tag;
			line->waiting = INVALID_REG;
		    line->next    = INVALID_CID;
		}
	}

    return DELAYED;
}

Result DCache::Read(MemAddr address, void* data, MemSize size, LFID /* fid */, RegAddr* reg)
{
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

	// Check that we're reading readable memory
	if (!m_parent.CheckPermissions(address, size, IMemory::PERM_READ))
	{
		throw SecurityException("Attempting to read from non-readable memory", *this);
	}

    Line*  line;
    Result result;
    // SUCCESS - A line with the address was found
    // DELAYED - The line with the address was not found, but a line has been allocated
    // FAILED  - No usable line was found at all
    if ((result = FindLine(address - offset, line, false)) == FAILED)
    {
        // Cache-miss and no free line
        return FAILED;
    }
    else if (result == DELAYED)
    {
        // Fetch the data
        if ((result = m_parent.ReadMemory(address - offset, line->data, m_lineSize, MemTag(line - &m_lines[0], true))) == FAILED)
        {
            return FAILED;
        }
    
        if (result == SUCCESS)
        {
            // Data has been loaded immediately
            COMMIT{ line->state = LINE_FULL; }
        }
    }
    else if (line->state == LINE_LOADING)
    {
        // The line is present but is being loaded
        result = DELAYED;
    }

    // Reset last line access
    COMMIT{ line->access  = m_parent.GetKernel().GetCycleNo(); }

    if (result == SUCCESS)
    {
        // Data was already in the cache or has been loaded immediately, copy it
        COMMIT
        {
            memcpy(data, line->data + offset, (size_t)size);
            m_numHits++;
        }
        return SUCCESS;
    }

    // Data is being loaded, add request to the queue
    assert(result == DELAYED);
	COMMIT
	{
		line->state = LINE_LOADING;
		if (reg != NULL && reg->valid())
		{
		    // We're loading to a valid register, queue it
    		RegAddr old   = line->waiting;
    		line->waiting = *reg;
    		*reg = old;
    		m_numWaiting++;
    	}
		m_numMisses++;
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
        throw InvalidArgumentException("Address range crosses over cache line boundary");
    }

#if MEMSIZE_MAX >= SIZE_MAX
    if (size > SIZE_MAX)
    {
        throw InvalidArgumentException("Size argument too big");
    }
#endif

	// Check that we're writing writable memory
	if (!m_parent.CheckPermissions(address, size, IMemory::PERM_WRITE))
	{
		throw SecurityException("Attempting to write to non-writable memory", *this);
	}

	COMMIT
	{
		Line* line;
		if (FindLine(address, line, true) == SUCCESS)
		{
			if (line->state == LINE_FULL) {
				// The line is cached and has no outstanding reads, update it.
				memcpy(line->data + offset, data, (size_t)size);
			} else {
			    assert(line->state == LINE_LOADING || line->state == LINE_PROCESSING);
			    
				// The line is present, but not yet loaded or still processing
				// outstanding reads, invalidate it.
				line->state = LINE_INVALID;
			}
		}
	}

    // Pass-through
    return m_parent.WriteMemory(address, data, size, MemTag(fid, tid));
}

bool DCache::OnMemoryReadCompleted(const MemData& data)
{
    assert(data.tag.data);
    assert(data.tag.cid != INVALID_CID);

    COMMIT
    {
        Line& line = m_lines[data.tag.cid];
        
        // Copy the data into the cache line
        memcpy(line.data, data.data, (size_t)data.size);

        // Push the cache-line to the back of the queue
        if (m_returned.head == INVALID_CID) {
            m_returned.head = data.tag.cid;
        } else {
            m_lines[m_returned.tail].next = data.tag.cid;
        }
        m_returned.tail = data.tag.cid;
		
		// It might have been invalidated because of a write, check state
		if (line.state != LINE_INVALID) {
		    assert(line.state == LINE_LOADING);
			line.state = LINE_PROCESSING;
		}
    }

    return true;
}

bool DCache::OnMemoryWriteCompleted(const MemTag& tag)
{
    // Data has been written
    if (!m_allocator.DecreaseThreadDependency(tag.fid, tag.tid, THREADDEP_OUTSTANDING_WRITES))
    {
        return false;
    }
    return true;
}

bool DCache::OnMemorySnooped(MemAddr address, const MemData& data)
{
    COMMIT
    {
        size_t offset = (size_t)(address % m_lineSize);
		Line*  line;

        // Cache coherency: check if we have the same address
		if (FindLine(address, line, true) == SUCCESS)
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

Result DCache::OnCycleWritePhase(unsigned int stateIndex)
{
    assert(stateIndex == 0);
    (void)stateIndex;
 
    if (m_returned.head == INVALID_CID)
    {
		// Nothing to do
		return DELAYED;
	}
	
	// Process a waiting register
    Line& line = m_lines[m_returned.head];
	if (line.waiting.valid())
	{
		// Write to register
		if (!m_regFile.p_asyncW.Write(line.waiting))
		{
			return FAILED;
		}

		// Read request information
		RegValue value;
		if (!m_regFile.ReadRegister(line.waiting, value))
		{
			return FAILED;
		}

		if (value.m_state == RST_FULL || value.m_memory.size == 0)
		{
			// Rare case: the request info is still in the pipeline, stall!
			return FAILED;
		}

        // Register must be in pending or waiting state
		assert(value.m_state == RST_EMPTY || value.m_state == RST_WAITING);

		// Ignore the request if the family has been killed
		const Family& family = m_familyTable[value.m_memory.fid];
		if (!family.killed)
		{
			// Write to register file
			uint64_t data = UnserializeRegister(line.waiting.type, &line.data[value.m_memory.offset], value.m_memory.size);

            // Number of registers that we're writing (must be a power of two)
            const size_t nRegs = (value.m_memory.size + sizeof(Integer) - 1) / sizeof(Integer);
            assert((nRegs & (nRegs - 1)) == 0);
            
            if (value.m_memory.sign_extend)
            {
                // Sign-extend the value
                assert(value.m_memory.size < sizeof(Integer));
                int shift = (sizeof(data) - value.m_memory.size) * 8;
                data = (int64_t)(data << shift) >> shift;
            }

   			RegAddr  addr = line.waiting;
   			RegValue reg;
   			reg.m_state = RST_FULL;

            for (size_t i = 0; i < nRegs; ++i)
            {
    			switch (addr.type) {
    			    case RT_INTEGER: reg.m_integer = (Integer)data; break;
    			    case RT_FLOAT:   reg.m_float.integer = (Integer)data; break;
    			}

                RegAddr a = addr;
#if ARCH_ENDIANNESS == ARCH_BIG_ENDIAN
                // LSB goes in last register
                a.index += (nRegs - 1 - i);
#else
                // LSB goes in first register
                a.index += i;
#endif

    			if (!m_regFile.WriteRegister(a, reg, true))
		    	{
	    			return FAILED;
    			}
    			
    			// We do this in two steps; otherwise the compiler could complain
    			// about shifting the whole data size.
                data >>= sizeof(Integer) * 4;
                data >>= sizeof(Integer) * 4;
    		}
        }

		if (!m_allocator.DecreaseFamilyDependency(value.m_memory.fid, FAMDEP_OUTSTANDING_READS))
		{
			return FAILED;
		}

		COMMIT{
		    line.waiting = value.m_memory.next;
            m_numWaiting--;
        }
	}

	if (!line.waiting.valid())
	{
	    // We're done with this line
	    COMMIT
	    {
    		if (line.state != LINE_INVALID) {
        		// If not invalidated, move the line from processing to full state
    		    assert(line.state == LINE_PROCESSING);
    			line.state = LINE_FULL;
    		} else {
    		    // Otherwise, move it to the empty state
    		    line.state = LINE_EMPTY;
    		}
    		m_returned.head = line.next;
        }
	}
	return SUCCESS;
}

void DCache::Cmd_Help(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Data Cache stores data from memory that has been used in loads and stores\n"
    "for faster access. Compared to a traditional cache, this D-Cache is extended\n"
    "with several fields to support the multiple threads and asynchronous operation.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the cache-lines, and global information such as hit-rate\n"
    "  and cache configuration.\n";
}

void DCache::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
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

    out << "Set |       Address       |                       Data                      |" << endl;
    out << "----+---------------------+-------------------------------------------------+" << endl;
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
            switch (line.state) {
                case LINE_LOADING:    out << "L"; break;
                case LINE_PROCESSING: out << "P"; break;
                case LINE_INVALID:    out << "I"; break;
                default:              out << " "; break;
            }
            out << " |";

            if (line.state != LINE_LOADING)
            {
                // Print the data
                out << hex << setfill('0');
                static const int BYTES_PER_LINE = 16;
                for (size_t y = 0; y < m_lineSize; y += BYTES_PER_LINE)
                {
                    for (size_t x = y; x < y + BYTES_PER_LINE; ++x) {
                        out << " " << setw(2) << (unsigned)(unsigned char)line.data[x];
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
                out << "                                                 |";
            }
        }
        out << endl;
        out << ((i + 1) % m_assoc == 0 ? "----" : "    ");
        out << "+---------------------+-------------------------------------------------+" << endl;
    }
}

}
