#include "Pipeline.h"
#include "Processor.h"
#include <cassert>
using namespace Simulator;
using namespace std;

Pipeline::PipeAction Pipeline::WritebackStage::read()
{
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::WritebackStage::write()
{
    bool     suspend         = (m_input.suspend != SUSPEND_NONE);
    uint64_t writebackValue  = m_writebackValue;
    RegSize  writebackSize   = m_writebackSize;
    RegIndex writebackOffset = m_writebackOffset;
    
    if (m_input.Rc.valid() && m_input.Rcv.m_state != RST_INVALID)
    {
        // We have something to write back
        RegValue value;
        value.m_state = m_input.Rcv.m_state;

        assert(m_input.Rcv.m_size % sizeof(Integer) == 0);
        RegSize nRegs = m_input.Rcv.m_size / sizeof(Integer);
        
        if (writebackSize == 0)
        {
            // New data write
            writebackSize   = nRegs;
            writebackOffset = 0;
            if (value.m_state == RST_FULL)
            {
                // New data write
                switch (m_input.Rc.type)
                {
                    case RT_INTEGER: writebackValue = m_input.Rcv.m_integer.get(m_input.Rcv.m_size); break;
                    case RT_FLOAT:   writebackValue = m_input.Rcv.m_float.toint(m_input.Rcv.m_size); break;
                }
            }
        }

        // Take data from input
        switch (value.m_state)
        {
        case RST_EMPTY:
            value.m_remote    = m_input.Rcv.m_remote;
            value.m_memory    = m_input.Rcv.m_memory;
            // Fall-through

        case RST_WAITING:
            value.m_waiting = m_input.Rcv.m_waiting;
            writebackSize   = 1;      // Write just one register
            nRegs           = 1;
            break;

        case RST_FULL:
            // Compose register value
            switch (m_input.Rc.type)
            {
                case RT_INTEGER: value.m_integer = (Integer)writebackValue; break;
                case RT_FLOAT:   value.m_float.integer = (Integer)writebackValue; break;
            }
            // We do this in two steps; otherwise the compiler could complain
            writebackValue >>= 4 * sizeof(Integer);
            writebackValue >>= 4 * sizeof(Integer);
            break;

        default:
            // These states are never written from the pipeline
            assert(0);
        }
        
        int offset = writebackOffset;
#ifdef ARCH_BIG_ENDIAN
        offset = nRegs - 1 - offset;
#endif

        if (m_input.Rrc.fid != INVALID_GFID)
        {
            assert(m_input.Rcv.m_state == RST_FULL);
            
            // Also forward the shared to the next CPU.
            // If we're the last thread in the family, it writes to the parent thread.
			if (!m_network.SendShared(
			    m_input.Rrc.fid,
			    m_input.isLastThreadInFamily,
			    MAKE_REGADDR(m_input.Rrc.reg.type, m_input.Rrc.reg.index + offset),
			    value))
            {
                return PIPE_STALL;
            }
        }
        
        // Get the address of the register we're writing.
        const RegAddr addr = MAKE_REGADDR(m_input.Rc.type, m_input.Rc.index + offset);
        
        // We have something to write back          
        if (!m_regFile.p_pipelineW.Write(*this, addr))
        {
            return PIPE_STALL;
        }
        
        // If we're writing WAITING and the data is already present,
        // Rcv's state will be set to FULL
        if (!m_regFile.WriteRegister(addr, value, false))
        {
            return PIPE_STALL;
        }
        
        suspend = (value.m_state == RST_WAITING);
        
        if (suspend)
        {
            // We're suspending because we're waiting on a non-full register.
            // Since we support multiple threads waiting on a register, update the
            // next field in the thread table to the next waiting thread.
            COMMIT{ m_threadTable[m_input.tid].nextState = value.m_waiting.head; }
        }

        // Adjust after writing
        writebackSize--;
        writebackOffset++;
    }
    else if (m_input.suspend == SUSPEND_MEMORY_BARRIER)
    {
        // Memory barrier, check to make sure it hasn't changed by now
        suspend = (m_threadTable[m_input.tid].dependencies.numPendingWrites != 0);
        if (suspend)
        {
            COMMIT{ m_threadTable[m_input.tid].waitingForWrites = true; }
        }
    }

    if (writebackSize == 0 && m_input.swch)
    {
        // We're done writing and we've switched threads
        if (m_input.kill)
        {
            // Kill the thread
            assert(suspend == false);
            if (!m_allocator.KillThread(m_input.tid))
            {
                return PIPE_STALL;
            }
        }
        // We're not killing, suspend or reschedule
        else if (suspend)
        {
            // Suspend the thread
            if (!m_allocator.SuspendThread(m_input.tid, m_input.pc))
            {
                return PIPE_STALL;
            }
        }
        // Reschedule thread
        else if (!m_allocator.RescheduleThread(m_input.tid, m_input.pc))
        {
            // We cannot reschedule, stall pipeline
            return PIPE_STALL;
        }
    }

    COMMIT {
        m_writebackValue  = writebackValue;
        m_writebackSize   = writebackSize;
        m_writebackOffset = writebackOffset;
    }
    
    return writebackSize == 0
        ? PIPE_CONTINUE     // We're done, continue
        : PIPE_DELAY;       // We still have data to write back next cycle
}

Pipeline::WritebackStage::WritebackStage(Pipeline& parent, MemoryWritebackLatch& input, RegisterFile& regFile, Network& network, Allocator& alloc, ThreadTable& threadTable)
  : Stage(parent, "writeback", &input, NULL),
    m_input(input),
    m_regFile(regFile),
    m_network(network),
    m_allocator(alloc),
    m_threadTable(threadTable),
    m_writebackSize(0)
{
}
