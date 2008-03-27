#include <cassert>
#include "Pipeline.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

Pipeline::PipeAction Pipeline::WritebackStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::WritebackStage::write()
{
    if (m_input.Rc.valid() && m_input.Rcv.m_state != RST_INVALID)
    {
        // We have something to write back  
        if (!m_regFile.p_pipelineW.write(*this, m_input.Rc))
        {
            return PIPE_STALL;
        }
        
        if (!m_regFile.writeRegister(m_input.Rc, m_input.Rcv, *this))
        {
            return PIPE_STALL;
        }

        if (m_input.Rrc.fid != INVALID_GFID)
        {
            assert(m_input.Rcv.m_state == RST_FULL);

            // Also forward the shared to the next CPU
            // If we're the last thread in the family, it writes to the parent thread
			if (!m_network.sendShared(m_input.Rrc.fid, m_input.isLastThreadInFamily, m_input.Rrc.reg, m_input.Rcv))
            {
                return PIPE_STALL;
            }
        }
    }

    if (m_input.swch && !m_allocator.decreaseFamilyDependency(m_input.fid, FAMDEP_THREADS_RUNNING))
    {
        return PIPE_STALL;
    }

    return PIPE_CONTINUE;
}

Pipeline::WritebackStage::WritebackStage(Pipeline& parent, MemoryWritebackLatch& input, RegisterFile& regFile, Network& network, Allocator& alloc)
  : Stage(parent, "writeback", &input, NULL),
    m_input(input),
    m_regFile(regFile),
    m_network(network),
    m_allocator(alloc)
{
}
