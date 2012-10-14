#include "Processor.h"
#include <arch/symtable.h>
#include <sim/sampling.h>
#include <sim/log2.h>
#include <programs/mgsim.h>
#include <arch/FPU.h>
#include <sim/breakpoints.h>

#include <cassert>
#include <cmath>
#include <cctype>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>

using namespace std;

namespace Simulator
{

/*static*/
RegValue Processor::Pipeline::ExecuteStage::PipeValueToRegValue(RegType type, const PipeValue& v)
{
    RegValue r;
    r.m_state = RST_FULL;
    switch (type)
    {
    case RT_INTEGER: r.m_integer       = v.m_integer.get(v.m_size); break;
    case RT_FLOAT:   r.m_float.integer = v.m_float.toint(v.m_size); break;
    default: assert(0); // should not be here
    }
    return r;
}

bool Processor::Pipeline::ExecuteStage::MemoryWriteBarrier(TID tid) const
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

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::OnCycle()
{
    COMMIT
    {
        // Copy common latch data
        (CommonData&)m_output = m_input;
        m_output.placeSize = m_input.placeSize;

        // Clear memory operation information
        m_output.address = 0;
        m_output.size    = 0;
        
        // Clear remote information
        m_output.Rrc.type = RemoteMessage::MSG_NONE;        
    }
    
    // If we need to suspend on an operand, it'll be in Rav (by the Read Stage)
    // In such a case, we must write them back, because they actually contain the
    // suspend information.
    if (m_input.Rav.m_state != RST_FULL)
    {
        COMMIT
        {
            // Write Rav (with suspend info) back to Rc.
            // The Remote Request information might contain a remote request
            // that will be send by the Writeback stage.
            m_output.Rc  = m_input.Rc;
            m_output.Rcv = m_input.Rav;
            
            // Force a thread switch
            m_output.swch    = true;
            m_output.kill    = false;
            m_output.suspend = SUSPEND_MISSING_DATA;
        }

        DebugPipeWrite("F%u/T%u(%llu) %s suspend on non-full operand %s",
                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                       m_input.Rc.str().c_str());

        return PIPE_FLUSH;
    }
    
    COMMIT
    {
        // Set PC to point to next instruction
        m_output.pc = m_input.pc + sizeof(Instruction);
        
        // Copy input data and set some defaults
        m_output.Rc          = m_input.Rc;
        m_output.suspend     = SUSPEND_NONE;
        m_output.Rcv.m_state = RST_INVALID;
        m_output.Rcv.m_size  = m_input.RcSize;
    }
    
    // Check for breakpoints
    GetKernel()->GetBreakPointManager().Check(BreakPointManager::EXEC, m_input.pc, *this);
    
    PipeAction action = ExecuteInstruction();
    if (action != PIPE_STALL)
    {
        // Operation succeeded
        COMMIT
        {
            // We've executed an instruction
            m_op++;
            if (action == PIPE_FLUSH)
            {
                // Pipeline was flushed, thus there's a thread switch
                m_output.swch = true;
                m_output.kill = false;
            }
        }

        DebugPipeWrite("F%u/T%u(%llu) %s executed Rc %s Rcv %s",
                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                       m_output.Rc.str().c_str(), m_output.Rcv.str(m_output.Rc.type).c_str());
    }
    else
    {
        DebugPipeWrite("F%u/T%u(%llu) %s stalled",
                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym);
    }
    
    return action;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecBundle(MemAddr addr, bool indirect, Integer value, RegIndex reg)
{
    if (indirect)
    {
        addr += m_parent.GetProcessor().ReadASR(ASR_SYSCALL_BASE);
    }

    if (!m_allocator.QueueBundle(addr, value, reg))
    {
        return PIPE_STALL;
    }

    if (reg != INVALID_REG_INDEX)
    {
        COMMIT { m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);  }
    }

    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecAllocate(PlaceID place, RegIndex reg, bool suspend, bool exclusive, Integer flags)
{
    if (place.size == 0)
    {
        // Inherit the parent's place
        place.size = m_input.placeSize;
        place.pid  = (m_parent.GetProcessor().GetPID() / place.size) * place.size;
        place.capability = 0x1337; // also later: copy the place capability from the parent.

        DebugSimWrite("F%u/T%u(%llu) %s adjusted default place -> CPU%u/%u cap 0x%lx",
                      (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                      (unsigned)place.pid, (unsigned)place.size, (unsigned long)place.capability);
    } 
    else if (place.size == 1 && place.capability == 0)
    {
        if (place.pid == 0)
        {
            // Local place
            place.pid  = m_parent.GetProcessor().GetPID();

            DebugSimWrite("F%u/T%u(%llu) %s adjusted local place -> CPU%u/1",
                          (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                          (unsigned)place.pid);
        }
        place.capability = 0x1337; // also later: copy the place capability from the parent.

        DebugSimWrite("F%u/T%u(%llu) %s adjusted sz 1 cap 0 -> 0x%lx",
                      (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                      (unsigned long)place.capability);
    }
    
    // Size must be a power of two and ID a multiple of size.
    assert(IsPowerOfTwo(place.size));
    assert((place.pid % place.size) == 0);

    // Verify processor ID
    if (place.pid >= m_parent.GetProcessor().GetGridSize())
    {
        throw SimulationException("Attempting to delegate to a non-existing core");
    }
    
    AllocationType type = (AllocationType)(flags & 3);
    if (exclusive && type != ALLOCATE_SINGLE)
    {
        type = ALLOCATE_SINGLE;
        
        DebugSimWrite("F%u/T%u(%llu) %s adjusted allocate type exclusive -> exclusive single",
                      (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym);
    }
        
    // Send an allocation request.
    // This will write back the FID to the specified register once the allocation
    // has completed. Even for creates to this core, we do this. Simplifies things.
    COMMIT
    {
        m_output.Rrc.type = RemoteMessage::MSG_ALLOCATE;
        m_output.Rrc.allocate.place          = place;
        m_output.Rrc.allocate.suspend        = suspend;
        m_output.Rrc.allocate.exclusive      = exclusive;
        m_output.Rrc.allocate.type           = type;
        m_output.Rrc.allocate.completion_pid = m_parent.GetProcessor().GetPID();
        m_output.Rrc.allocate.completion_reg = reg;
            
        m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
    }
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::SetFamilyProperty(const FID& fid, FamilyProperty property, Integer value)
{
    COMMIT
    {
        m_output.Rrc.type = RemoteMessage::MSG_SET_PROPERTY;
        m_output.Rrc.property.fid   = fid;
        m_output.Rrc.property.type  = property;
        m_output.Rrc.property.value = value;
    }
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecCreate(const FID& fid, MemAddr address, RegIndex completion)
{
    // Create
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
    
    // Send the create.
    COMMIT
    {
        m_output.Rrc.type = RemoteMessage::MSG_CREATE;
        m_output.Rrc.create.fid            = fid;
        m_output.Rrc.create.address        = address;
        m_output.Rrc.create.completion_reg = completion;
        m_output.Rrc.create.bundle         = false;
        
        m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
    }
    
    DebugFlowWrite("F%u/T%u(%llu) %s create CPU%u/F%u %s",
                   (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                   (unsigned)fid.pid, (unsigned) fid.lfid,
                   m_parent.GetProcessor().GetSymbolTable()[address].c_str());
    
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ReadFamilyRegister(RemoteRegType kind, RegType type, const FID& fid, unsigned char reg)
{
    COMMIT
    {
        m_output.Rrc.type = RemoteMessage::MSG_FAM_REGISTER;
        m_output.Rrc.famreg.write = false;
        m_output.Rrc.famreg.kind = kind;
        m_output.Rrc.famreg.fid  = fid;
        m_output.Rrc.famreg.addr = MAKE_REGADDR(type, reg);
            
        m_output.Rrc.famreg.value.m_state = RST_INVALID;
        m_output.Rrc.famreg.completion_reg = m_output.Rc.index;
        m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
    }
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::WriteFamilyRegister(RemoteRegType kind, RegType type, const FID& fid, unsigned char reg)
{
    COMMIT
    {
        m_output.Rrc.type = RemoteMessage::MSG_FAM_REGISTER;
        m_output.Rrc.famreg.write = true;
        m_output.Rrc.famreg.kind = kind;
        m_output.Rrc.famreg.fid  = fid;
        m_output.Rrc.famreg.addr = MAKE_REGADDR(type, reg);
            
        assert(m_input.Rbv.m_size == sizeof(Integer));
        m_output.Rrc.famreg.value = PipeValueToRegValue(type, m_input.Rbv);
    }
    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecSync(const FID& fid)
{
    assert(m_input.Rc.type == RT_INTEGER);
    
    if (m_input.Rc.index == INVALID_REG_INDEX)
    {
        throw exceptf<InvalidArgumentException>(*this, "F%u/T%u(%llu) %s invalid target register for sync", 
                                                (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym);
    }

    if (fid.pid == 0 && fid.lfid == 0 && fid.capability == 0)
    {
        COMMIT {
            m_output.Rcv.m_integer = 0;
            m_output.Rcv.m_state   = RST_FULL;
        }
    }
    else
    {
        // Send the sync message
        COMMIT
        {
            m_output.Rrc.type                = RemoteMessage::MSG_SYNC;
            m_output.Rrc.sync.fid            = fid;
            m_output.Rrc.sync.completion_reg = m_input.Rc.index;
            
            m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);            
        }
        DebugFlowWrite("F%u/T%u(%llu) %s sync CPU%u/F%u",
                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                       (unsigned)fid.pid, (unsigned)fid.lfid);
    }

    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecDetach(const FID& fid)
{
    if (fid.pid == 0 && fid.lfid == 0 && fid.capability == 0)
    {
        /* no-op */
    }
    else
    {
        // Send the detach message
        COMMIT
        {
            m_output.Rrc.type = RemoteMessage::MSG_DETACH;
            m_output.Rrc.detach.fid = fid;
        }
        DebugFlowWrite("F%u/T%u(%llu) %s detach CPU%u/F%u",
                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                       (unsigned)fid.pid, (unsigned)fid.lfid);
    }

    return PIPE_CONTINUE;
}

Processor::Pipeline::PipeAction Processor::Pipeline::ExecuteStage::ExecBreak()
{ 
    COMMIT
    {
             
        m_output.Rrc.type    = RemoteMessage::MSG_BREAK;
        m_output.Rrc.brk.pid = m_parent.GetProcessor().GetPID();
        m_output.Rrc.brk.fid = m_input.fid;
    }
    return PIPE_CONTINUE; 
}

void Processor::Pipeline::ExecuteStage::ExecDebugOutput(Integer value, int command, int flags) const
{
    // command:
    //  0 -> unsigned decimal
    //  1 -> hex
    //  2 -> signed decimal
    //  3 -> ASCII character

    // flags: - - S S
    // S = output stream
    //  0 -> debug output
    //  1 -> standard output
    //  2 -> standard error
    //  3 -> (undefined)
    int outstream = flags & 3;

    ostringstream stringout;
    ostream& out = (outstream == 0) ? stringout : ((outstream == 2) ? cerr : cout);

    switch (command)
    {
    case 0: out << dec << value; break;
    case 1: out << hex << value; break;
    case 2: out << dec << (SInteger)value; break;
    case 3: out << (char)value; break;
    }
    out << flush;

    if (outstream == 0)
    {
        DebugProgWrite("F%u/T%u(%llu) %s PRINT: %s",
                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                       stringout.str().c_str());
    }
}


void Processor::Pipeline::ExecuteStage::ExecStatusAction(Integer value, int command, int flags) const
{
    // command:
    //  00: status and continue
    //  01: status and interrupt
    //  10: status and abort
    //  11: status and exit with code

    // flags: - - S S
    // S = output stream
    //  0 -> debug output
    //  1 -> standard output
    //  2 -> standard error
    //  3 -> (undefined)


    int outstream = flags & 3;

    ostringstream stringout;
    ostream& out = (outstream == 0) ? stringout : ((outstream == 2) ? cerr : cout);

    Integer msg = value;
    unsigned i;
    for (i = 0; i < sizeof(msg); ++i, msg >>= 8)
    {
        char byte = msg & 0xff;
        if (std::isprint(byte)) out << byte;
    }

    if (outstream == 0)
    {
        DebugProgWrite("F%u/T%u(%llu) %s STATUS: %s",
                       (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                       stringout.str().c_str());
    }

    switch(command)
    {
    case 0: 
        break;
    case 1: 
        // interrupt the simulation in a way that is resumable
        // from the interactive prompt.
        GetKernel()->Stop();
        break;
    case 2:
        abort();
        break;
    case 3:
    {
        int code = value & 0xff;
        exit(code);
        break;
    }
    }    
}

void Processor::Pipeline::ExecuteStage::ExecMemoryControl(Integer value, int command, int flags) const
{
    // command:
    //  00: mmap(addr = value, size = 2^(flags+12), pid = 0)
    //  01: munmap(addr = value, size = 2^(flags+12))
    //  10: mmap(addr = value, size = 2^(flags+12), pid = ASR_PID)
    //  11, flag 0: munmapall(pid = value)
    //  11, flag 1: set ASR_PID := value
    
    unsigned l = flags & 0x7;
    MemSize req_size = 1 << (l + 12);

    Processor& cpu = m_parent.GetProcessor();

    switch(command)
    {
    case 0:
        cpu.MapMemory(value, req_size);
        break;
    case 1:
        cpu.UnmapMemory(value, req_size);
        break;
    case 2:
        cpu.MapMemory(value, req_size, cpu.ReadASR(ASR_PID));
        break;
    case 3:
        if (flags == 0)
            cpu.WriteASR(ASR_PID, value);
        else if (flags == 1)
            cpu.UnmapMemory(value);
        break;
    }    
}

void Processor::Pipeline::ExecuteStage::ExecDebug(Integer value, Integer stream) const
{
    // pattern: x x 0 1 x x x x = status and action
    // pattern: 0 0 0 1 - - - - =   status and continue
    // pattern: 0 1 0 1 - - - - =   status and interrupt
    // pattern: 1 0 0 1 - - - - =   status and abort
    // pattern: 1 1 0 1 - - - - =   status and exit with code
    // pattern: - - 0 1 - - 0 0 =   debug channel
    // pattern: - - 0 1 - - 0 1 =   stdout channel
    // pattern: - - 0 1 - - 1 0 =   stderr channel
    // pattern: - - 0 1 - - 0 0 =   channel: (unused)

    // pattern: x x 0 0 1 x x x = memory control
    // pattern: 0 0 0 0 1 L L L =   map with pid in APR
    // pattern: 0 1 0 0 1 L L L =   unmap
    // pattern: 1 0 0 0 1 0 0 0 =   unmap all with pid in reg

    // pattern: x x 0 0 0 - x x = debug output
    // pattern: 0 0 0 0 0 - - - =   output unsigned dec
    // pattern: 0 1 0 0 0 - - - =   output hex
    // pattern: 1 0 0 0 0 - - - =   output signed dec
    // pattern: 1 1 0 0 0 - - - =   output ASCII byte
    // pattern: - - 0 0 0 - 0 0 =   debug channel
    // pattern: - - 0 0 0 - 0 1 =   stdout channel
    // pattern: - - 0 0 0 - 1 0 =   stderr channel
    // pattern: - - 0 0 0 - 0 0 =   channel: (unused)

    // std::cerr << "CTL: stream " << std::hex << stream << " value " << value << std::dec << std::endl;

    int command = (stream >> 6) & 0x3;
    if ((stream & 0x30) == 0x10)
        ExecStatusAction(value, command, stream & 0xF);
    else if ((stream & 0x38) == 0x8)
        ExecMemoryControl(value, command, stream & 0x7);
    else if ((stream & 0x38) == 0)
        ExecDebugOutput(value, command, stream & 0x3);
}

void Processor::Pipeline::ExecuteStage::ExecDebug(double value, Integer stream) const
{
    /* precision: bits 4-7 */
    int prec = (stream >> 4) & 0xf;
    int s = stream & 3;
    switch (s) {
    case 0:
      DebugProgWrite("F%u/T%u(%llu) %s PRINT: %0.*lf",
                     (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index, m_input.pc_sym,
                     prec, value );
      break;
    case 1:
    case 2:
      ostream& out = (s == 2) ? cerr : cout;
        out << setprecision(prec) << scientific << value;
    break;
    }
}

Processor::Pipeline::ExecuteStage::ExecuteStage(Pipeline& parent, Clock& clock, const ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& alloc, FamilyTable& familyTable, ThreadTable& threadTable, FPU& fpu, size_t fpu_source, Config& /*config*/)
  : Stage("execute", parent, clock),
    m_input(input),
    m_output(output),
    m_allocator(alloc),
    m_familyTable(familyTable),
    m_threadTable(threadTable),
    m_fpu(fpu),
    m_fpuSource(fpu_source)
{
    m_flop = 0;
    m_op   = 0;
    RegisterSampleVariableInObject(m_flop, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_op, SVC_CUMULATIVE);
}

}
