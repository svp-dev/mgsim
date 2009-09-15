#include "Pipeline.h"
#include "Processor.h"
#include "gfx.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;

namespace Simulator
{

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

Pipeline::PipeAction Pipeline::ExecuteStage::OnCycle()
{
    COMMIT
    {
        // Copy common latch data
        (CommonData&)m_output = m_input;

        // Clear memory operation information
        m_output.address     = 0;
        m_output.size        = 0;        
    }
    
    // If we need to suspend on an operand, it'll be in Rav (by the Read Stage)
    // In such a case, we must write them back, because they actually contain the
    // suspend information.
    if (m_input.Rav.m_state != RST_FULL)
    {
        if (m_input.Rra.fid != INVALID_LFID)
        {
            // We need to request this register remotely
            if (!m_network.RequestRegister(m_input.Rra, m_input.fid))
            {
                DeadlockWrite("Unable to request register for operand");
                return PIPE_STALL;
            }
        }

        COMMIT
        {
            // Write Rav (with suspend info) back to Rc.
            m_output.Rc  = m_input.Rc;
            m_output.Rcv = m_input.Rav;
            
            // Disable a potentional remote write for this instruction.
            m_output.Rrc.fid = INVALID_LFID;
            
            // Force a thread switch
            m_output.swch    = true;
            m_output.kill    = false;
            m_output.suspend = SUSPEND_MISSING_DATA;
        }
        return PIPE_FLUSH;
    }
    
    COMMIT
    {
        // Set PC to point to next instruction
        m_output.pc = m_input.pc + sizeof(Instruction);
        
        // Copy input data and set some defaults
        m_output.Rc          = m_input.Rc;
        m_output.Rrc         = m_input.Rrc;
        m_output.suspend     = SUSPEND_NONE;
        m_output.Rcv.m_state = RST_INVALID;
        m_output.Rcv.m_size  = m_input.Rcv.m_size;
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
       	m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_output.Rcv.m_size);
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
    	}
    }
	return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::ExecBreak(Integer /* value */) { return PIPE_CONTINUE; }
Pipeline::PipeAction Pipeline::ExecuteStage::ExecBreak(double /* value */)  { return PIPE_CONTINUE; }
Pipeline::PipeAction Pipeline::ExecuteStage::ExecKill(LFID /* fid */)       { return PIPE_CONTINUE; }

  template<typename T, unsigned bits>
  struct scaler {};


  template<typename T>
  struct scaler<T, 4> {
static  void scale_pp(size_t& x, size_t& y, uint32_t& data, T value)
  {
	x = (value >> 24) & 0xff;
	y = (value >> 16) & 0xff;
	data = ((value & 0xf000) << (4*4))
	  | ((value & 0x0f00) << (3*4))
	  | ((value & 0x00f0) << (2*4))
	  | ((value & 0x000f) << (1*4));
  }

    static void scale_rs(size_t& w, size_t& h, T value)
    {
      w = (value >> 24) & 0xff;
      h = (value >> 16) & 0xff;
    }
  };

  template<typename T>
  struct scaler<T, 8> {
    static void scale_pp(size_t& x, size_t& y, uint32_t& data, T value)
  {
	x = (value >> 48) & 0xffff;
	y = (value >> 32) & 0xffff;
	data = value & 0xffffffff;
  }
    static void scale_rs(size_t& w, size_t& h, T value)
    {
      w = (value >> 48) & 0xffff;
      h = (value >> 32) & 0xffff;
    }
  };

void Pipeline::ExecuteStage::ExecDebug(Integer value, Integer stream) const
{
  if ((stream >> 5) & 1) {
    GfxDisplay *disp = GetKernel()->getDisplay();
    assert(disp != NULL);
    int command = (stream >> 6) & 3;
    //    std::cerr << "Gfx command = " << command << std::endl;
    if (command == 0) {
      // put pixel
      size_t x, y;
      uint32_t data;
      scaler<Integer, sizeof(value)>::scale_pp(x, y, data, value);
      disp->getFrameBuffer().putPixel(x, y, data);
    } else if (command == 1) {
      // resize screen
      size_t w, h;
      scaler<Integer, sizeof(value)>::scale_rs(w, h, value);
      disp->resizeFrameBuffer(w, h);
    }
  }
  else {
  int outstream = stream & 3;

  ostringstream stringout;
  ostream& out = (outstream == 0) ? stringout : \
    ((outstream == 2) ? cerr : cout);

  switch ((stream >> 6) & 0x3) {
  case 0:
    out << dec << (unsigned long long)value; break;
  case 1:
    out << hex << (unsigned long long)value; break;
  case 2:
    out << dec << (long long)(SInteger)value; break;
  case 3:
    out << (char)value; break;
  }

  if (outstream == 0)
    DebugProgWrite("PRINT by T%u at 0x%.*llx: %s",
		   (unsigned)m_input.tid, (int)sizeof(m_input.pc) * 2, (unsigned long long)m_input.pc,
		   stringout.str().c_str());
  }
}

void Pipeline::ExecuteStage::ExecDebug(double value, Integer stream) const
{
    /* precision: bits 4-7 */
    int prec = (stream >> 4) & 0xf;
    int s = stream & 3;
    switch (s) {
    case 0:
      DebugProgWrite("PRINT by T%u at 0x%.*llx: %0.*lf",
		     (unsigned)m_input.tid, (int)sizeof(m_input.pc) * 2, (unsigned long long)m_input.pc,
		     prec, value );
      break;
    case 1:
    case 2:
      ostream& out = (s == 2) ? cerr : cout;
        out << setprecision(prec) << scientific << value;
	break;
    }
}

Pipeline::ExecuteStage::ExecuteStage(Pipeline& parent, const ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& alloc, Network& network, ThreadTable& threadTable, FPU& fpu, size_t fpu_source, const Config& /*config*/)
  : Stage(parent, "execute"),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_network(network),
    m_threadTable(threadTable),
	m_fpu(fpu),
	m_fpuSource(fpu_source)
{
    m_flop = 0;
    m_op   = 0;
}

}
