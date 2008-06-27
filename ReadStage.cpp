#include <cassert>
#include "Pipeline.h"
#include "Processor.h"
#include "Network.h"
using namespace Simulator;
using namespace std;

bool Pipeline::ReadStage::readRegister(RegAddr reg, ReadPort& port, RegValue& val, bool& RvFromExec)
{
    if (reg.invalid())
    {
        val.m_state = RST_FULL;
        switch (reg.type)
        {
			case RT_INTEGER: val.m_integer = 0; break;
			case RT_FLOAT:   val.m_float.fromfloat(0.0f); break;
        }
    }
    // Check the bypasses
    else if (!m_output.empty() && m_output.Rc == reg)
    {
        // Execute stage will produce the value this cycle
        // We will have to grab it in the write phase, when the Execute stage has executed
        COMMIT{ RvFromExec = true; }
    }
    else if (!m_bypass1.empty() && m_bypass1.Rc == reg)
    {
        // Execute stage result has the value
        val = m_bypass1.Rcv;
    }
    else if (!m_bypass2.empty() && m_bypass2.Rc == reg)
    {
        // Memory stage result has the value
        val = m_bypass2.Rcv;
    }
    // No data from bypasses, read the register
    else if (!port.read(*this))
    {
        return false;
    }
    else if (!m_regFile.readRegister(reg, val))
    {
        return false;
    }

    // Data was read
    return true;
}

Pipeline::PipeAction Pipeline::ReadStage::read()
{
    COMMIT
    {
        m_ravFromExec = false;
        m_rbvFromExec = false;
    }

    if (!readRegister(m_input.Ra, m_regFile.p_pipelineR1, m_rav, m_ravFromExec))
    {
        return PIPE_STALL;
    }

    if (m_input.Rb.invalid())
    {
        // Use the literal instead
        COMMIT
        {
            m_rbv.m_integer = m_input.literal;
            m_rbv.m_state   = RST_FULL;
        }
    }
    else if (!readRegister(m_input.Rb, m_regFile.p_pipelineR2, m_rbv, m_rbvFromExec))
    {
        return PIPE_STALL;
    }
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ReadStage::write()
{
    RegValue rav = (m_ravFromExec) ? m_bypass1.Rcv : m_rav;
    RegValue rbv = (m_rbvFromExec) ? m_bypass1.Rcv : m_rbv;

    COMMIT
    {
        // Copy common latch data
        (CommonLatch&)m_output = m_input;
                
		m_output.fpcr           = m_input.fpcr;
        m_output.format         = m_input.format;
        m_output.opcode         = m_input.opcode;
        m_output.function       = m_input.function;
        m_output.displacement   = m_input.displacement;
        m_output.literal        = m_input.literal;
        m_output.Rc             = m_input.Rc;
        m_output.Rrc            = m_input.Rrc;
        m_output.Rav            = rav;
        m_output.Rbv            = rbv;

        for (RegType i = 0; i < NUM_REG_TYPES; i++)
        {
            m_output.familyRegs[i] = m_input.familyRegs[i];
            m_output.threadRegs[i] = m_input.threadRegs[i];
        }
    }

    if (rav.m_state != RST_FULL)
    {
        // Register wasn't full, write back the suspend information
        if (m_input.Rra.fid != INVALID_GFID)
        {
            // Send a remote request
			DebugSimWrite("Requesting shared %s for G%u\n", m_input.Rra.reg.str().c_str(), m_input.Rra.fid);
			if (!m_network.requestShared(m_input.Rra.fid, m_input.Rra.reg, m_input.isFirstThreadInFamily))
            {
                return PIPE_STALL;
            }
        }
        
        COMMIT
        {
            m_output.Rc      = m_input.Ra;
            m_output.Rrc.fid = INVALID_GFID;
			m_output.Rav.m_tid   = m_input.tid;
            m_output.Rav.m_state = RST_WAITING;
        }
    }
    else if (rbv.m_state != RST_FULL)
    {
        // Register wasn't full, write back the suspend information
        if (m_input.Rrb.fid != INVALID_GFID)
        {
            // Send a remote request
			DebugSimWrite("Requesting remote shared %s for family G%u\n", m_input.Rrb.reg.str().c_str(), m_input.Rrb.fid);
			if (!m_network.requestShared(m_input.Rrb.fid, m_input.Rrb.reg, m_input.isFirstThreadInFamily))
            {
                return PIPE_STALL;
            }
        }

        COMMIT
        {
            m_output.Rc      = m_input.Rb;
            m_output.Rrc.fid = INVALID_GFID;
            m_output.Rbv.m_tid   = m_input.tid;
            m_output.Rbv.m_state = RST_WAITING;
        }
    }

    return PIPE_CONTINUE;
}

Pipeline::ReadStage::ReadStage(Pipeline& parent, DecodeReadLatch& input, ReadExecuteLatch& output, RegisterFile& regFile, Network& network, ExecuteMemoryLatch& bypass1, MemoryWritebackLatch& bypass2)
  : Stage(parent, "read", &input, &output),
    m_regFile(regFile),
    m_network(network),
    m_input(input),
    m_output(output),
    m_bypass1(bypass1),
    m_bypass2(bypass2)
{
}
