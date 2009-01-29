#include <cassert>
#include "Pipeline.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

// Memory reads and writes will be of at most this many bytes
static const int MAX_MEMORY_OPERATION_SIZE = 8;

Pipeline::PipeAction Pipeline::MemoryStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::MemoryStage::write()
{
    RegValue rcv = m_input.Rcv;

    if (m_input.size > 0)
    {
        // It's a memory operation!
        Result result = SUCCESS;

        if (m_input.Rcv.m_state == RST_FULL)
        {
            // Memory write

            // Serialize and store data
            char data[MAX_MEMORY_OPERATION_SIZE];
            SerializeRegister(m_input.Rc.type, m_input.Rcv, data, (size_t)m_input.size);

            if ((result = m_dcache.write(m_input.address, data, m_input.size, m_input.fid, m_input.tid)) == FAILED)
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
            rcv.m_state = RST_EMPTY;
        }
        else if (m_input.Rc.valid())
        {
            char data[MAX_MEMORY_OPERATION_SIZE];
			RegAddr reg = m_input.Rc;
            if ((result = m_dcache.read(m_input.address, data, m_input.size, m_input.fid, &reg)) == FAILED)
            {
                // Stall
                return PIPE_STALL;
            }

            if (result == SUCCESS)
            {
                // Unserialize and store data
                rcv = UnserializeRegister(m_input.Rc.type, data, (size_t)m_input.size);
            }
			else
			{
				// Remember request data
	            rcv.m_state          = RST_PENDING;
				rcv.m_request.fid    = m_input.fid;
				rcv.m_request.next   = reg;
				rcv.m_request.offset = (unsigned int)(m_input.address % m_dcache.getLineSize());
				rcv.m_request.size   = (size_t)m_input.size;
				rcv.m_component      = &m_dcache;
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
            else if (!m_allocator.IncreaseFamilyDependency(m_input.fid, FAMDEP_OUTSTANDING_READS))
            {
                return PIPE_STALL;
            }
        }
    }

    COMMIT
    {
        // Copy common latch data
        (CommonLatch&)m_output = m_input;
        
        m_output.suspend = m_input.suspend;
        m_output.Rc      = m_input.Rc;
        m_output.Rrc     = m_input.Rrc;
        m_output.Rcv     = rcv;
    }   
    return PIPE_CONTINUE;
}

Pipeline::MemoryStage::MemoryStage(Pipeline& parent, ExecuteMemoryLatch& input, MemoryWritebackLatch& output, DCache& dcache, Allocator& alloc, RegisterFile& regFile, FamilyTable& familyTable)
  : Stage(parent, "memory", &input, &output),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_dcache(dcache),
    m_regFile(regFile),
    m_familyTable(familyTable)
{
}
