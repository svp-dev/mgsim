#include "Pipeline.h"
#include "Processor.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
using namespace Simulator;
using namespace std;

bool Pipeline::ExecuteStage::MemoryWriteBarrier(TID tid) const
{
    Thread& thread = m_threadTable[tid];
    if (thread.dependencies.numPendingWrites != 0)
    {
        // There are pending writes, we need to wait for them
        assert(!thread.waitingForWrites);
        return false;
    }
    return true;
}

Pipeline::PipeAction Pipeline::ExecuteStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::write()
{
    COMMIT
    {
        // Copy common latch data
        (Latch&)m_output = m_input;

        m_output.Rc          = m_input.Rc;
        m_output.Rrc         = m_input.Rrc;
        m_output.address     = 0;
        m_output.size        = 0;
        m_output.suspend     = SUSPEND_NONE;
        m_output.Rcv.m_state = RST_INVALID;
        m_output.Rcv.m_size  = m_input.Rcv.m_size;
    }
    
    // Check if both operands are available.
    // If not, we must write them back, because they actually contain the
    // suspend information.
    if (m_input.Rav.m_state != RST_FULL || m_input.Rbv.m_state != RST_FULL)
    {
        const RemoteRegAddr& rr = (m_input.Rav.m_state != RST_FULL) ? m_input.Rra : m_input.Rrb;
        if (rr.fid != INVALID_LFID)
        {
            // We need to request this register remotely
            if (!m_network.RequestRegister(rr, m_input.fid))
            {
                DeadlockWrite("Unable to request register for operand");
                return PIPE_STALL;
            }
        }

        COMMIT
        {
            m_output.Rcv     = (m_input.Rav.m_state != RST_FULL) ? m_input.Rav : m_input.Rbv;
            m_output.swch    = true;
            m_output.suspend = SUSPEND_MISSING_DATA;
            m_output.kill    = false;
        }
        return PIPE_FLUSH;
    }
    
    COMMIT
    {
        // Set PC to point to next instruction
        m_output.pc = m_input.pc + sizeof(Instruction);
    }
    
    PipeAction action = ExecuteInstruction();
    COMMIT
    {
        if (action != PIPE_STALL)
        {
            // Operation succeeded
            // We've executed an instruction
            m_op++;
            if (action == PIPE_FLUSH)
            {
                // Pipeline was flushed, thus there's a thread switch
                m_output.swch = true;
                m_output.kill = false;
            }
        }
    }
    
    return action;
}

Pipeline::PipeAction Pipeline::ExecuteStage::ExecCreate(LFID fid, MemAddr address, RegAddr exitCodeReg)
{
    assert(exitCodeReg.type == RT_INTEGER);

    // Direct create
	if (!MemoryWriteBarrier(m_input.tid))
	{
	    // We need to wait for the pending writes to complete
	    COMMIT
	    {
    	    m_output.pc      = m_input.pc;
        	m_output.suspend = SUSPEND_MEMORY_BARRIER;
        	m_output.swch    = true;
        	m_output.kill    = false;
        	m_output.Rc      = INVALID_REG;
        }
        return PIPE_FLUSH;
	}
	
    if (!m_allocator.QueueCreate(fid, address, m_input.tid, exitCodeReg.index))
   	{
   		return PIPE_STALL;
   	}
   	
    COMMIT
    {
       	m_output.Rcv = MAKE_EMPTY_PIPEVALUE(m_output.Rcv.m_size);
    }
	return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::SetFamilyProperty(LFID fid, FamilyProperty property, uint64_t value)
{
    Family& family = m_allocator.GetWritableFamilyEntry(fid, m_input.tid);
    COMMIT
    {
        switch (property)
        {
            case FAMPROP_START: family.start         = (SInteger)value; break;
            case FAMPROP_LIMIT: family.limit         = (SInteger)value; break;
            case FAMPROP_STEP:  family.step          = (SInteger)value; break;
    		case FAMPROP_BLOCK: family.virtBlockSize = (TSize)value; break;
    		case FAMPROP_PLACE:
    		{
    		    // Unpack the place value: <Capability:N, PID:P, EX:1>
    		    unsigned int P = (unsigned int)ceil(log2(m_parent.GetProcessor().GetGridSize()));
    		    
    		    family.place.exclusive  = (((value >> 0) & 1) != 0);
    		    family.place.type       = (PlaceID::Type)((value >> 1) & 3);
    		    family.place.pid        = (GPID)((value >> 3) & ((1ULL << P) - 1));
    		    family.place.capability = value >> (P + 3);
    		    
    		    if (family.place.type == PlaceID::DELEGATE && family.place.pid >= m_parent.GetProcessor().GetGridSize())
    		    {
    		        throw SimulationException("Attempting to delegate to a non-existing core");
    		    }
    		    break;
    		}
    	}
    }
	return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::SetFamilyRegs(LFID fid, const Allocator::RegisterBases bases[])
{
    Family& family = m_allocator.GetWritableFamilyEntry(fid, m_input.tid);

	// Set the base for the shareds and globals in the parent thread
	COMMIT
	{
	    for (RegType i = 0; i < NUM_REG_TYPES; ++i)
	    {
        	family.regs[i].globals = bases[i].globals;
            family.regs[i].shareds = bases[i].shareds;
        }
    }
	return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::ExecBreak(Integer /* value */) { return PIPE_CONTINUE; }
Pipeline::PipeAction Pipeline::ExecuteStage::ExecBreak(double /* value */)  { return PIPE_CONTINUE; }
Pipeline::PipeAction Pipeline::ExecuteStage::ExecKill(LFID /* fid */)       { return PIPE_CONTINUE; }

void Pipeline::ExecuteStage::ExecDebug(Integer value, Integer stream) const
{
    if ((stream & 0x7f) == 0) {
        if (stream & 0x80) {
            DebugProgWrite("PRINT by T%u at 0x%0.*llx: %lld",
                m_input.tid, sizeof(m_input.pc) * 2, (unsigned long long)m_input.pc,
                (long long)(SInteger)value);
        } else {
            DebugProgWrite("PRINT by T%u at 0x%0.*llx: %llu",
                m_input.tid, sizeof(m_input.pc) * 2, (unsigned long long)m_input.pc,
                (unsigned long long)value);
        }
    } else {
        ostream& out = (stream != 1) ? cerr : cout;
        out << (char)value;
    }
}

void Pipeline::ExecuteStage::ExecDebug(double value, Integer stream) const
{
    if (stream == 0) {
        DebugProgWrite("PRINT by T%u at 0x%0.*llx: %0.12f",
            m_input.tid,
            sizeof(m_input.pc) * 2, (unsigned long long)m_input.pc,
            value );
    } else {
        ostream& out = (stream != 1) ? cerr : cout;
        out << setprecision(12) << fixed << value;
    }
}

Pipeline::ExecuteStage::ExecuteStage(Pipeline& parent, ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& alloc, Network& network, ThreadTable& threadTable, FPU& fpu, const Config& /*config*/)
  : Stage(parent, "execute", &input, &output),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_network(network),
    m_threadTable(threadTable),
	m_fpu(fpu)
{
    m_flop = 0;
    m_op   = 0;
}
