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
:   IComponent(&parent, parent.GetKernel(), name, "completed-reads|completed-writes|outgoing"), m_parent(parent),
	m_allocator(alloc), m_familyTable(familyTable), m_regFile(regFile),
	m_assoc          (config.getInteger<size_t>("DCacheAssociativity", 4)),
	m_sets           (config.getInteger<size_t>("DCacheNumSets", 4)),
	m_lineSize       (config.getInteger<size_t>("CacheLineSize", 64)),
	m_returned       (parent.GetKernel(), m_lines),
	m_completedWrites(parent.GetKernel(), config.getInteger<BufferSize>("DCacheCompletedWriteBufferSize", INFINITE)),
	m_outgoing       (parent.GetKernel(), config.getInteger<BufferSize>("DCacheOutgoingBufferSize", 1)),
	m_numHits        (0),
	m_numMisses      (0),
	p_service        (*this, "p_service")
{
	m_returned       .Sensitive(*this, 0);
	m_completedWrites.Sensitive(*this, 1);
	m_outgoing       .Sensitive(*this, 2);

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
    
    m_wbstate.size   = 0;
    m_wbstate.offset = 0;
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

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for D-Cache access");
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
    COMMIT{ line->access = m_parent.GetKernel().GetCycleNo(); }

    if (result == DELAYED)
    {
        // A new line has been allocated; send the request to memory
        Request request;
        request.write     = false;
        request.address   = address - offset;
        request.data.size = m_lineSize;
        request.data.tag  = MemTag(line - &m_lines[0], true);
        if (!m_outgoing.Push(request))
        {
            DeadlockWrite("Unable to push request to outgoing buffer");
            return FAILED;
        }
    }
    else if (line->state != LINE_LOADING)
    {
        // Data was already in the cache, copy it
        COMMIT
        {
            memcpy(data, line->data + offset, (size_t)size);
            m_numHits++;
        }
        return SUCCESS;
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

    if (!p_service.Invoke())
    {
        DeadlockWrite("Unable to acquire port for D-Cache access");
        return FAILED;
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

    // Store request for memory
    Request request;
    request.write     = true;
    request.address   = address;
    memcpy(request.data.data, data, size);
    request.data.size = size;
    request.data.tag  = MemTag(fid, tid);
    if (!m_outgoing.Push(request))
    {
        DeadlockWrite("Unable to push request to outgoing buffer");
        return FAILED;
    }
    return DELAYED;
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

		// It might have been invalidated because of a write, check state
		if (line.state != LINE_INVALID) {
		    assert(line.state == LINE_LOADING);
			line.state = LINE_PROCESSING;
		}
    }
    
    // Push the cache-line to the back of the queue
    m_returned.Push(data.tag.cid);
    return true;
}

bool DCache::OnMemoryWriteCompleted(const MemTag& tag)
{
    // Data has been written
    if (!m_completedWrites.Push(tag))
    {
        DeadlockWrite("Unable to push write completion to buffer");
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

Result DCache::OnCycle(unsigned int stateIndex)
{
    switch (stateIndex)
    {
    case 0:
    {
        assert(!m_returned.Empty());
	
        if (!p_service.Invoke())
        {
            DeadlockWrite("Unable to acquire port for D-Cache access in read completion");
            return FAILED;
        }

	    // Process a waiting register
        Line& line = m_lines[m_returned.Front()];
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
		        const Family& family = m_familyTable[value.m_memory.fid];
	    	    if (!family.killed)
    		    {
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
            }
            m_returned.Pop();
	    }
	    return SUCCESS;
	}
	    
    case 1:
    {
        assert(!m_completedWrites.Empty());
        const MemTag& tag = m_completedWrites.Front();
        if (!m_allocator.DecreaseThreadDependency(tag.fid, tag.tid, THREADDEP_OUTSTANDING_WRITES))
        {
            DeadlockWrite("Unable to decrease outstanding writes on T%u in F%u", (unsigned)tag.tid, (unsigned)tag.fid);
            return FAILED;
        }
        m_completedWrites.Pop();
        return SUCCESS;
    }
    
    case 2:
    {
        assert(!m_outgoing.Empty());
        const Request& request = m_outgoing.Front();
        if (request.write)
        {
            if (!m_parent.WriteMemory(request.address, request.data.data, request.data.size, request.data.tag))
            {
                DeadlockWrite("Unable to send write of %u bytes to 0x%016llx to memory", (unsigned)request.data.size, (unsigned long long)request.address);
                return FAILED;
            }
        }
        else
        {
            if (!m_parent.ReadMemory(request.address, request.data.size, request.data.tag))
            {
                DeadlockWrite("Unable to send read of %u bytes to 0x%016llx to memory", (unsigned)request.data.size, (unsigned long long)request.address);
                return FAILED;
            }
        }
        m_outgoing.Pop();
        return SUCCESS;
    }
    }

    return DELAYED;
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
