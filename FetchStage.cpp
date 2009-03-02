#include "Pipeline.h"
#include "Processor.h"
#include "SimpleMemory.h"
#include <cassert>
using namespace Simulator;
using namespace std;

void Pipeline::FetchStage::clear(TID tid)
{
    if (m_tid == tid)
    {
        m_tid = INVALID_TID;
    }
}

Pipeline::PipeAction Pipeline::FetchStage::read()
{
    if (m_tid == INVALID_TID)
    {
        // Get the next active thread
        TID tid = m_allocator.m_activeThreads.head;
        if (tid == INVALID_TID)
        {
			// No thread, nothing to do
			return PIPE_IDLE;
		}

        // Get the family and thread information
        const Thread& thread = m_threadTable[tid];
		const Family& family = m_familyTable[thread.family];

		if (!family.killed)
		{
			MemAddr pc = thread.pc;
			if (!family.legacy && pc % m_controlBlockSize == 0)
			{
				// Skip the control word
				pc += sizeof(Instruction);
			}

			// Read the cache line for this PC
			size_t offset = (size_t)(pc % m_icache.GetLineSize());   // Offset within the cacheline
			if (!m_icache.Read(thread.cid, pc - offset, m_buffer, m_icache.GetLineSize()))
			{
				return PIPE_STALL;
			}

			COMMIT
			{
				m_pc  = pc;
			    m_legacy                = family.legacy;
				m_isLastThreadInBlock   = thread.isLastThreadInBlock;
				m_isLastThreadInFamily  = thread.isLastThreadInFamily;
				m_isFirstThreadInFamily = thread.isFirstThreadInFamily;
				m_onParent              = (family.parent.pid == m_parent.GetProcessor().GetPID());

				for (RegType i = 0; i < NUM_REG_TYPES; i++)
				{
					m_regs.types[i].family = family.regs[i];
					m_regs.types[i].thread = thread.regs[i];
				}

				m_switched = true;
			}
        }

		COMMIT
		{
			m_fid  = thread.family;
			m_gfid = family.gfid;
			m_tid  = tid;
		}
	}
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::FetchStage::write()
{
    if (m_tid == INVALID_TID)
    {
        // There are no active families and threads
        return PIPE_IDLE;
    }

	Thread& thread = m_threadTable[m_tid];
	Family& family = m_familyTable[m_fid];

	if (family.killed)
	{
		// If the family has been killed, don't continue
		return PIPE_STALL;
	}

	size_t offset   = (size_t)(m_pc % m_icache.GetLineSize());              // Offset within the cacheline
	size_t iInstr   = offset / sizeof(Instruction);                         // Offset in instructions
	size_t iControl = (offset & -m_controlBlockSize) / sizeof(Instruction); // Align offset down to control block size

	Instruction* instrs = (Instruction*)m_buffer;
	Instruction control = (!m_legacy) ? UnserializeInstruction(&instrs[iControl]) >> (2 * (iInstr - iControl)) : 0;

	if (m_switched)
	{
		// We've just switched to this thread
		COMMIT
		{
			// Pop the active thread
			m_next = m_allocator.PopActiveThread(m_tid);
		}

		COMMIT
		{
			// Mark the thread as running
			thread.state = TST_RUNNING;
		}
	}

	COMMIT
	{
		m_output.instr = UnserializeInstruction(&instrs[iInstr]);
		m_output.fid   = m_fid;
		m_output.gfid  = m_gfid;
		m_output.tid   = m_tid;
		m_output.pc    = m_pc;
		m_output.onParent = m_onParent;
		m_output.isLastThreadInFamily  = m_isLastThreadInFamily;
		m_output.isLastThreadInBlock   = m_isLastThreadInBlock;
		m_output.isFirstThreadInFamily = m_isFirstThreadInFamily;

		m_pc += sizeof(Instruction);

		m_output.kill   = ((control & 2) != 0);
		bool wantSwitch = ((control & 1) != 0);
		bool mustSwitch = m_output.kill || (m_pc % m_icache.GetLineSize() == 0);
		m_output.swch   = mustSwitch;
		m_output.regs   = m_regs;

		if (mustSwitch || (wantSwitch && m_next != INVALID_TID))
		{
			// Switch to another thread next cycle.
			// Don't switch if there are no other threads!
			m_tid         = INVALID_TID;
			m_output.swch = true;
		}

		m_switched = false;
	}

	return PIPE_CONTINUE;
}

Pipeline::FetchStage::FetchStage(Pipeline& parent, FetchDecodeLatch& fdLatch, Allocator& alloc, FamilyTable& familyTable, ThreadTable& threadTable, ICache& icache, size_t controlBlockSize)
  : Stage(parent, "fetch", NULL, &fdLatch),
    m_output(fdLatch),
    m_allocator(alloc),
    m_familyTable(familyTable),
    m_threadTable(threadTable),
    m_icache(icache)
{
    if ((controlBlockSize & ~(controlBlockSize - 1)) != controlBlockSize)
    {
        throw InvalidArgumentException("Control block size is not a power of two");
    }

    if (controlBlockSize > 64)
    {
        throw InvalidArgumentException("Control block size is larger than 64");
    }

    if (controlBlockSize > m_icache.GetLineSize())
    {
        throw InvalidArgumentException("Control block size is larger than the cache line size");
    }

    m_tid = INVALID_TID;
    m_buffer = new char[m_icache.GetLineSize()];
    m_controlBlockSize = (int)controlBlockSize;
}

Pipeline::FetchStage::~FetchStage()
{
    delete[] m_buffer;
}

