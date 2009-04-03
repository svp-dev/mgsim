#include "Pipeline.h"
#include "Processor.h"
#include <limits>
#include <cassert>
using namespace Simulator;
using namespace std;

Pipeline::Stage::Stage(Pipeline& parent, const std::string& name, Latch* input, Latch* output)
:   IComponent(&parent, parent.GetProcessor().GetKernel(), name, ""),    // Stages don't require callbacks
    m_parent(parent), m_input(input), m_output(output)
{
}

Pipeline::Pipeline(
    Processor&          parent,
    const std::string&  name,
    LPID                lpid,
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
    IComponent(&parent, parent.GetKernel(), name, "writeback|memory|execute|read|decode|fetch"), m_parent(parent), m_regFile(regFile),
    m_nStagesRun(0), m_maxPipelineIdleTime(0), m_minPipelineIdleTime(numeric_limits<uint64_t>::max()),
    m_totalPipelineIdleTime(0), m_pipelineIdleEvents(0), m_pipelineIdleTime(0), m_pipelineBusyTime(0),
    
    m_fetch    (*this, m_fdLatch, alloc, familyTable, threadTable, icache, lpid, config.controlBlockSize),
    m_decode   (*this, m_fdLatch, m_drLatch),
    m_read     (*this, m_drLatch, m_reLatch, regFile, network, m_emLatch, m_mwLatch),
    m_execute  (*this, m_reLatch, m_emLatch, alloc, threadTable, familyTable, fpu),
    m_memory   (*this, m_emLatch, m_mwLatch, dcache, alloc, regFile, familyTable),
    m_writeback(*this, m_mwLatch, regFile, network, alloc, threadTable)
{
    m_stages[0] = &m_fetch;
    m_stages[1] = &m_decode;
    m_stages[2] = &m_read;
    m_stages[3] = &m_execute;
    m_stages[4] = &m_memory;
    m_stages[5] = &m_writeback;
}

Result Pipeline::OnCycleReadPhase(unsigned int stateIndex)
{   
    if (IsAcquiring() && stateIndex == 0)
    {
        // Begin of the cycle, initialize
        m_nStagesRunnable = 0;
        for (int i = 0; i < NUM_STAGES; ++i)
        {
            Latch* input = m_stages[i]->getInput();
            m_runnable[i] = (input == NULL || !input->empty());
        }
    }

    // Execute stages from back to front
    int stage = NUM_STAGES - 1 - stateIndex;
    if (m_runnable[stage])
    {
        // This stage can read
        PipeAction action = m_stages[stage]->read();
		if (action != PIPE_IDLE)
		{
            COMMIT{
                m_nStagesRunnable++;
            }
            
			if (action == PIPE_STALL || action == PIPE_DELAY)
			{
				// This stage has stalled, abort pipeline
				for (int i = 0; i < stage; ++i)
				{
					m_runnable[i] = false;
				}
				return (action == PIPE_STALL) ? FAILED : SUCCESS;
			}
			
			if (action == PIPE_FLUSH)
			{
				COMMIT
				{
					// Clear all previous stages with the same TID
					TID tid = m_stages[stage]->getInput()->tid;
					for (int j = 0; j < stage; ++j)
					{
						Latch* input = m_stages[j]->getInput();
						if (input != NULL && input->tid == tid)
						{
							input->clear();
						}
						m_stages[j]->clear(tid);
					}
				}
			}
	        return SUCCESS;
		}
    }

	// This stage has nothing to do
    return DELAYED;
}

Result Pipeline::OnCycleWritePhase(unsigned int stateIndex)
{
    int stage = NUM_STAGES - 1 - stateIndex;
    if (m_runnable[stage])
    {
        // This stage can execute
        PipeAction action = m_stages[stage]->write();
		if (action != PIPE_IDLE)
		{
		    if (!IsAcquiring())
		    {
			    if (action == PIPE_STALL || action == PIPE_DELAY)
    			{
    				// This stage has stalled or is delayed, abort pipeline
    				for (int i = 0; i < stage; ++i)
    				{
    					m_runnable[i] = false;
    				}
    				return (action == PIPE_STALL) ? FAILED : SUCCESS;
	    		}

		    	Latch* input  = m_stages[stage]->getInput();
    			Latch* output = m_stages[stage]->getOutput();

	    		if (action == PIPE_FLUSH)
    			{
    				// Clear all previous stages with the same TID
    				for (int j = 0; j < stage; ++j)
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
    			{
                    m_nStagesRun++;
    				if (input  != NULL) input->clear();
    				if (output != NULL) output->set();
    			}
			}
	        return SUCCESS;
		}
    }

	// This stage has nothing to do
    return DELAYED;
}

void Pipeline::UpdateStatistics()
{
    if (m_nStagesRunnable == 0)
    {
        m_pipelineIdleTime++;
    }
    else
    {
        m_pipelineBusyTime++;
        
        if (m_pipelineIdleTime > 0)
        {
            // Process this pipeline idle streak
            m_maxPipelineIdleTime    = max(m_maxPipelineIdleTime, m_pipelineIdleTime);
            m_minPipelineIdleTime    = min(m_minPipelineIdleTime, m_pipelineIdleTime);
            m_totalPipelineIdleTime += m_pipelineIdleTime;
            m_pipelineIdleEvents++;
            m_pipelineIdleTime = 0;
        }
    }
}
