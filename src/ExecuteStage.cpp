#include "Pipeline.h"
#include "Processor.h"
#include "display.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>

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

void Pipeline::ExecuteStage::ExecDebug(Integer value, Integer stream) const
{
    if (stream & 0x20)
    {
        // stream: C C 1 - - - - -
        // C = command
        //  0 -> putpixel
        //  1 -> resize
        //  2 -> snapshot
        switch ((stream >> 6) & 3)
        {
        case 0:
        {
            // put pixel
            // stream: 0 0 1 M - - - -
            // M = mode
            //  0 -> pixel at (x,y)
            //  1 -> pixel at offset
            // value: position(16) X(4) R(4) G(4) B(4) || position(32) X(8) R(8) G(8) B(8)
            const unsigned int  size     = sizeof value * 8;
            const unsigned int  bits     = std::min(8U, size / 8);
            const unsigned long position = value >> (size / 2);
            const uint32_t data = (((value >> (0 * size / 8)) << ( 8 - bits)) & 0x0000ff) |
                                  (((value >> (1 * size / 8)) << (16 - bits)) & 0x00ff00) |
                                  (((value >> (2 * size / 8)) << (24 - bits)) & 0xff0000);
            if (stream & 0x10) {
                // position: offset(16) || offset(32)
                m_display.PutPixel(position, data);
            } else {
                // position: x(8) y(8) || x(16) y(16)
                const unsigned int m = (1U << (size / 4)) - 1;
                const unsigned int y = (position >> (0 * size / 4)) & m;
                const unsigned int x = (position >> (1 * size / 4)) & m;
                m_display.PutPixel(x, y, data);
            }
            break;
        }
            
        case 1:
        {
            // resize screen
            // stream: 0 1 1 - - - - -
            // value:  W(8) H(8) unused(16)  ||  W(16) H(16) unused(32)
            const size_t       size = sizeof value * 8;
            const unsigned int mask = (1U << (size / 4)) - 1;
            const unsigned int w    = (value >> (3 * size / 4)) & mask;
            const unsigned int h    = (value >> (2 * size / 4)) & mask;
            m_display.Resize(w, h);
            break;
        }
        
        case 2:
        {
            // take screenshot
            // stream: 1 0 1 T C - S S
            // T = embed timestamp in filename
            // C = embed thread information in picture comment
            // S = output stream
            //  0 -> file
            //  1 -> standard output
            //  2 -> standard error
            //  3 -> (undefined)
            // value: file identification key
            ostringstream tinfo;
            if (stream & 8)
            {
                tinfo << "print by thread 0x" 
                      << std::hex << (unsigned)m_input.tid 
                      << " at 0x" << (unsigned long long)m_input.pc
                      << " on cycle " << std::dec << GetKernel()->GetCycleNo();
            }
            
            int outstream = stream & 3;
            if (outstream == 0)
            {
                ostringstream fname;
                fname << "gfx." << value;
                if (stream & 0x10)
                {
                    fname << '.' << GetKernel()->GetCycleNo();
                }
                fname << ".ppm";
                ofstream f(fname.str().c_str(), ios_base::out | ios_base::trunc);
                m_display.Dump(f, value, tinfo.str());
            }
            else
            {
                ostream& out = (outstream == 2) ? cerr : cout;
                m_display.Dump(out, value, tinfo.str());
            }
            break;
        }
        }
    }
    else
    {
        // stream: F F 0 - - - S S
        // F = format
        //  0 -> unsigned decimal
        //  1 -> hex
        //  2 -> signed decimal
        //  3 -> ASCII character
        // S = output stream
        //  0 -> debug output
        //  1 -> standard output
        //  2 -> standard error
        //  3 -> (undefined)
        int outstream = stream & 3;

        ostringstream stringout;
        ostream& out = (outstream == 0) ? stringout : ((outstream == 2) ? cerr : cout);

        switch ((stream >> 6) & 0x3)
        {
        case 0: out << dec << value; break;
        case 1: out << hex << value; break;
        case 2: out << dec << (SInteger)value; break;
        case 3: out << (char)value; break;
        }
        out << flush;

        if (outstream == 0)
        {
            DebugProgWrite("PRINT by T%u at 0x%.*llx: %s",
                (unsigned)m_input.tid, (int)sizeof(m_input.pc) * 2, (unsigned long long)m_input.pc,
                stringout.str().c_str());
        }
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

Pipeline::ExecuteStage::ExecuteStage(Pipeline& parent, const ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& alloc, Network& network, ThreadTable& threadTable, Display& display, FPU& fpu, size_t fpu_source, const Config& /*config*/)
  : Stage("execute", parent),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_network(network),
    m_threadTable(threadTable),
    m_display(display),
    m_fpu(fpu),
    m_fpuSource(fpu_source)
{
    m_flop = 0;
    m_op   = 0;
}

}
