#include "Pipeline.h"
#include "Processor.h"
#include <cassert>
using namespace std;

namespace Simulator
{

Pipeline::PipeAction Pipeline::WritebackStage::Write()
{
    bool     suspend         = (m_input.suspend != SUSPEND_NONE);
    RegSize  writebackSize   = m_writebackSize;
    RegIndex writebackOffset = m_writebackOffset;
    
    if (m_input.Rc.valid() && m_input.Rcv.m_state != RST_INVALID)
    {
        // We have something to write back
        assert(m_input.Rcv.m_size % sizeof(Integer) == 0);
        RegSize nRegs = m_input.Rcv.m_size / sizeof(Integer);
        
        if (writebackSize == 0)
        {
            // New data write
            writebackSize   = nRegs;
            writebackOffset = 0;
        }

        // Take data from input
        RegValue value = MAKE_EMPTY_REG();
        value.m_state = m_input.Rcv.m_state;
        switch (value.m_state)
        {
        case RST_WAITING:
        case RST_EMPTY:
            // We store the remote information in all registers.
            // Adjust the shared index for the offset accordingly.
            value.m_remote = m_input.Rcv.m_remote;
            value.m_remote.reg.index += writebackOffset;
            if (writebackOffset == 0)
            {
                // Store the thread and memory information in the first register only
                value.m_waiting = m_input.Rcv.m_waiting;
                value.m_memory  = m_input.Rcv.m_memory;
            }
            break;

        case RST_FULL:
        {
            // Compose register value
            unsigned int index = writebackOffset;
#ifdef ARCH_BIG_ENDIAN
            index = writebackSize - 1 - index;
#endif
            const unsigned int shift = index * 8 * sizeof(Integer);
            
            switch (m_input.Rc.type)
            {
                case RT_INTEGER: value.m_integer       = (Integer)(m_input.Rcv.m_integer.get(m_input.Rcv.m_size) >> shift); break;
                case RT_FLOAT:   value.m_float.integer = (Integer)(m_input.Rcv.m_float.toint(m_input.Rcv.m_size) >> shift); break;
            }
            break;
        }
        
        default:
            // These states are never written from the pipeline
            assert(0);
        }
        
        // Get the address of the register we're writing.
        const RegAddr addr = MAKE_REGADDR(m_input.Rc.type, m_input.Rc.index + writebackOffset);
        
        // We have something to write back
        if (!m_regFile.p_pipelineW.Write(addr))
        {
            return PIPE_STALL;
        }
        
        // Read the old value
        RegValue old_value;
        if (!m_regFile.ReadRegister(addr, old_value))
        {
            return PIPE_STALL;
        }

        if (value.m_state == RST_WAITING)
        {
            assert(suspend);
            
            if (old_value.m_state == RST_FULL)
            {
                // The data we wanted to wait for has returned before we could write the register.
                // Just reschedule the thread.
                suspend = false;
                value.m_state = RST_INVALID;
            }
            else
            {
                assert(value.m_waiting.head == m_input.tid);
                
                // The Read Stage will have setup the register to 
                // link this thread into the register's thread waiting list
                if (old_value.m_state == RST_WAITING && old_value.m_waiting.head != INVALID_TID)
                {
                    // Not the first thread waiting on the register
                    // Update the tail
                    assert(value.m_waiting.tail == old_value.m_waiting.tail);
                }
                else
                {
                    assert(value.m_waiting.tail == m_input.tid);
                }
                
                COMMIT
                {
                    // We're suspending because we're waiting on a non-full register.
                    // Since we support multiple threads waiting on a register, update the
                    // next field in the thread table to the next waiting thread.
                    m_threadTable[m_input.tid].nextState = old_value.m_waiting.head;
                }
            }
        }
        else if (value.m_state == RST_EMPTY && old_value.m_state == RST_WAITING)
        {
            // "Resetting a waiting register" can occur when starting a long-latency
            // operation on a shared register that already has a thread waiting on it.
            // So we just combine the waiting information.
            assert(old_value.m_remote.fid  == INVALID_LFID);
            assert(old_value.m_memory.size == 0);
            
            value.m_state   = RST_WAITING;
            value.m_waiting = old_value.m_waiting;
        }
        
        if (value.m_state != RST_FULL && old_value.m_state != RST_FULL)
        {
            // The remote waiting state might have changed since the pipeline.
            // If the register's now waiting remotely, copy that information.
            // Note: this cannot happen for the memory state (m_memory), because
            // that is only written from the pipeline, so the read stage would've
            // picked it up from the bypass bus.
            if (value.m_remote.fid == INVALID_LFID)
            {
                value.m_remote = old_value.m_remote;
            }
            else if (old_value.m_remote.fid != INVALID_LFID)
            {
                // If we're writing back a remote state as well, it must match what's already there
                assert(value.m_remote.type == old_value.m_remote.type);
                assert(value.m_remote.pid  == old_value.m_remote.pid);
                assert(value.m_remote.reg  == old_value.m_remote.reg);
                assert(value.m_remote.fid  == old_value.m_remote.fid);
            }
        }

        if (value.m_state != RST_INVALID)
        {
            if (!m_regFile.WriteRegister(addr, value, false))
            {
                return PIPE_STALL;
            }
        }
        
        // Check if this value should be forwarded.
        // If there is a remote write waiting on this register, we never forward.
        // The register also has to be full (empty registers can be written on
        // long-latency operations to shareds).
        if (m_input.Rrc.fid != INVALID_LFID &&
            (old_value.m_state == RST_FULL || old_value.m_remote.fid == INVALID_LFID) &&
            m_input.Rcv.m_state == RST_FULL
           )
        {
            // Forward the value to the next CPU.
            RemoteRegAddr rrc(m_input.Rrc);
			rrc.reg.index += writebackOffset;
            
			if (!m_network.SendRegister(rrc, value))
            {
                return PIPE_STALL;
            }
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
        m_writebackSize   = writebackSize;
        m_writebackOffset = writebackOffset;
    }
    
    return writebackSize == 0
        ? PIPE_CONTINUE     // We're done, continue
        : PIPE_DELAY;       // We still have data to write back next cycle
}

Pipeline::WritebackStage::WritebackStage(Pipeline& parent, const MemoryWritebackLatch& input, RegisterFile& regFile, Network& network, Allocator& alloc, ThreadTable& threadTable, const Config& /*config*/)
  : Stage(parent, "writeback"),
    m_input(input),
    m_regFile(regFile),
    m_network(network),
    m_allocator(alloc),
    m_threadTable(threadTable),
    m_writebackSize(0)
{
}

}
