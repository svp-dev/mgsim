#include <cassert>
#include "Pipeline.h"
#include "Processor.h"
using namespace Simulator;
using namespace std;

Pipeline::Stage::Stage(Pipeline& parent, const std::string& name, Latch* input, Latch* output)
:   IComponent(&parent, parent.getProcessor().getKernel(), name, 0),    // Stages don't require callbacks
    m_parent(parent), m_input(input), m_output(output)
{
}

Pipeline::Pipeline(
    Processor&          parent,
    const std::string&  name,
    RegisterFile&       regFile,
    Network&            network,
    Allocator&          alloc,
    FamilyTable&        familyTable,
    ThreadTable&        threadTable,
    ICache&             icache,
    DCache&             dcache,
	FPU&                fpu,
    const Config&       config)
:
    IComponent(&parent, parent.getKernel(), name, NUM_STAGES), m_parent(parent), m_regFile(regFile),
    m_fetch(*this, m_fdLatch, alloc, familyTable, threadTable, icache, config.controlBlockSize),
    m_decode(*this, m_fdLatch, m_drLatch),
    m_read(*this, m_drLatch, m_reLatch, regFile, network, m_emLatch, m_mwLatch),
    m_execute(*this, m_reLatch, m_emLatch, alloc, familyTable, threadTable, icache, fpu),
    m_memory(*this, m_emLatch, m_mwLatch, dcache, alloc, regFile, familyTable),
    m_writeback(*this, m_mwLatch, regFile, network, alloc)
{
    m_stages[0] = &m_fetch;
    m_stages[1] = &m_decode;
    m_stages[2] = &m_read;
    m_stages[3] = &m_execute;
    m_stages[4] = &m_memory;
    m_stages[5] = &m_writeback;
}

bool Pipeline::idle() const
{
    for (int i = 1; i < NUM_STAGES; i++)
    {
        if (!m_stages[i]->getInput()->empty())
        {
            return false;
        }
    }
    return true;
}

Result Pipeline::onCycleReadPhase(int stateIndex)
{   
    if (acquiring() && stateIndex == 0)
    {
        // Begin of the cycle, initialize
        for (int i = 0; i < NUM_STAGES; i++)
        {
            Latch* input = m_stages[i]->getInput();
            m_runnable[i] = (input == NULL || !input->empty());
        }
    }

    int stage = NUM_STAGES - 1 - stateIndex;
    if (m_runnable[stage])
    {
        // This stage can read
        PipeAction action = m_stages[stage]->read();
		if (action != PIPE_IDLE)
		{
			if (action == PIPE_STALL)
			{
				// This stage has stalled, abort pipeline
				for (int i = 0; i < stage; i++)
				{
					m_runnable[i] = false;
				}
				return FAILED;
			}
			
			if (action == PIPE_FLUSH)
			{
				COMMIT
				(
					// Clear all previous stages with the same TID
					Latch* input = m_stages[stage]->getInput();
					for (int j = 0; j < stage; j++)
					{
						Latch* in = m_stages[j]->getInput();
						if (in != NULL && in->tid == input->tid)
						{
							in->clear();
						}
						m_stages[j]->clear(input->tid);
					}
				)
			}
	        return SUCCESS;
		}
    }

	// This stage has nothing to do
    return DELAYED;
}

Result Pipeline::onCycleWritePhase(int stateIndex)
{
    int stage = NUM_STAGES - 1 - stateIndex;
    if (m_runnable[stage])
    {
        // This stage can execute
        PipeAction action = m_stages[stage]->write();
		if (action != PIPE_IDLE)
		{
			if (action == PIPE_STALL)
			{
				// This stage has stalled, abort pipeline
				for (int i = 0; i < stage; i++)
				{
					m_runnable[i] = false;
				}
				return FAILED;
			}

			Latch* input  = m_stages[stage]->getInput();
			Latch* output = m_stages[stage]->getOutput();

			if (action == PIPE_FLUSH)
			{
				// Clear all previous stages with the same TID
				for (int j = 0; j < stage; j++)
				{
					Latch* in = m_stages[j]->getInput();
					if (in != NULL && in->tid == input->tid)
					{
						in->clear();
						m_runnable[j] = false;
					}

					m_stages[j]->clear(input->tid);
				}
			}

			COMMIT
			(
				if (input  != NULL) input->clear();
				if (output != NULL) output->set();
			)
	        return SUCCESS;
		}
    }

	// This stage has nothing to do
    return DELAYED;
}
