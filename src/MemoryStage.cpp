#include "Pipeline.h"
#include "Processor.h"
#include <cassert>
using namespace Simulator;
using namespace std;

// Memory reads and writes will be of at most this many bytes
static const size_t MAX_MEMORY_OPERATION_SIZE = 8;

Pipeline::PipeAction Pipeline::MemoryStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::MemoryStage::write()
{
    PipeValue rcv = m_input.Rcv;
    
    if (m_input.size > 0)
    {
        // It's a new memory operation!
        assert(m_input.size <= MAX_MEMORY_OPERATION_SIZE);
        assert(MAX_MEMORY_OPERATION_SIZE <= sizeof(uint64_t));

        Result result = SUCCESS;
        if (rcv.m_state == RST_FULL)
        {
            // Memory write

            // Serialize and store data
            char data[MAX_MEMORY_OPERATION_SIZE];

            uint64_t value = 0;
            switch (m_input.Rc.type) {
                case RT_INTEGER: value = m_input.Rcv.m_integer.get(m_input.Rcv.m_size); break;
                case RT_FLOAT:   value = m_input.Rcv.m_float.toint(m_input.Rcv.m_size); break;
                default: assert(0);
            }
            
            SerializeRegister(m_input.Rc.type, value, data, (size_t)m_input.size);

            if ((result = m_dcache.Write(m_input.address, data, m_input.size, m_input.fid, m_input.tid)) == FAILED)
            {
                // Stall
                return PIPE_STALL;
            }

            // Clear the register state so it won't get written to the register file
            rcv.m_state = RST_INVALID;
        }
        // Memory read
        else if (m_input.address == numeric_limits<MemAddr>::max())
        {
            // Invalid address; don't send request, just clear register
            rcv = MAKE_EMPTY_PIPEVALUE(rcv.m_size);
        }
        else if (m_input.Rc.valid())
        {
            // Memory read
            char data[MAX_MEMORY_OPERATION_SIZE];
			RegAddr reg = m_input.Rc;
            if ((result = m_dcache.Read(m_input.address, data, m_input.size, m_input.fid, &reg)) == FAILED)
            {
                // Stall
                return PIPE_STALL;
            }

            rcv.m_size = m_input.Rcv.m_size;
            if (result == SUCCESS)
            {
                // Unserialize and store data
                uint64_t value = UnserializeRegister(m_input.Rc.type, data, (size_t)m_input.size);

                if (m_input.sign_extend)
                {
                    // Sign-extend the value
                    size_t shift = (sizeof(value) - (size_t)m_input.size) * 8;
                    value = (int64_t)(value << shift) >> shift;
                }
                
                rcv.m_state = RST_FULL;
                switch (m_input.Rc.type)
                {
                case RT_INTEGER: rcv.m_integer.set(value, rcv.m_size); break;
                case RT_FLOAT:   rcv.m_float.fromint(value, rcv.m_size); break;
                default:         assert(0);
                }
            }
			else
			{
				// Remember request data
	            rcv = MAKE_EMPTY_PIPEVALUE(rcv.m_size);
				rcv.m_memory.fid         = m_input.fid;
				rcv.m_memory.next        = reg;
				rcv.m_memory.offset      = (unsigned int)(m_input.address % m_dcache.GetLineSize());
				rcv.m_memory.size        = (size_t)m_input.size;
				rcv.m_memory.sign_extend = m_input.sign_extend;
				rcv.m_remote             = m_input.Rrc;
			}
        }

        if (result == DELAYED)
        {
            // Increase the oustanding memory count for the family
            if (m_input.Rcv.m_state == RST_FULL)
            {
                if (!m_allocator.IncreaseThreadDependency(m_input.tid, THREADDEP_OUTSTANDING_WRITES))
                {
                    return PIPE_STALL;
                }
            }
            else if (!m_allocator.OnMemoryRead(m_input.fid))
            {
                return PIPE_STALL;
            }
        }
    }

    COMMIT
    {
        // Copy common latch data
        (Latch&)m_output = m_input;
        
        m_output.suspend = m_input.suspend;
        m_output.Rc      = m_input.Rc;
        m_output.Rrc     = m_input.Rrc;
        m_output.Rcv     = rcv;
    }
    
    return PIPE_CONTINUE;
}

Pipeline::MemoryStage::MemoryStage(Pipeline& parent, ExecuteMemoryLatch& input, MemoryWritebackLatch& output, DCache& dcache, Allocator& alloc)
  : Stage(parent, "memory", &input, &output),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_dcache(dcache)
{
}
