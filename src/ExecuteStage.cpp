#include "Pipeline.h"
#include "Processor.h"
#include "display.h"
#include "symtable.h"

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

static RegValue PipeValueToRegValue(RegType type, const PipeValue& v)
{
    RegValue r;
    r.m_state = RST_FULL;
    switch (type)
    {
    case RT_INTEGER: r.m_integer       = v.m_integer.get(v.m_size); break;
    case RT_FLOAT:   r.m_float.integer = v.m_float.toint(v.m_size); break;
    }
    return r;
}

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
    
    PipeAction action = ExecuteInstruction();
    if (action != PIPE_STALL)
    {
        // Operation succeeded
        COMMIT
        {
            if (m_input.shared.offset != -1)
            {
                // Writing a shared
                if (m_output.Rcv.m_state != RST_FULL)
                {
                    // Writing a non-full value to a shared, this is NOT a good thing
                    throw SimulationException("Writing a non-full value to a shared register. Do you have a shared as target for a long-latency instruction?");
                }
                
                // Compose a remote message
                assert(m_output.Rrc.type == RemoteMessage::MSG_NONE);
                m_output.Rrc.type = RemoteMessage::MSG_REGISTER;
                
                m_output.Rrc.reg.addr.type     = RRT_NEXT_DEPENDENT;
                m_output.Rrc.reg.addr.fid.lfid = m_familyTable[m_input.fid].link_next;
                m_output.Rrc.reg.addr.fid.pid  = INVALID_GPID;
                m_output.Rrc.reg.addr.reg      = MAKE_REGADDR(m_input.shared.type, m_input.shared.offset);
                m_output.Rrc.reg.value         = PipeValueToRegValue(m_input.shared.type, m_output.Rcv);
            }            
            
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

bool Pipeline::ExecuteStage::MoveFamilyRegister(RemoteRegType kind, RegType type, const FID& fid, unsigned char reg)
{
    if (fid.pid != m_parent.GetProcessor().GetPID())
    {
        // Send a network message
        assert(m_input.Rbv.m_size == sizeof(Integer));
        
        COMMIT
        {
            m_output.Rrc.type = RemoteMessage::MSG_REGISTER;
            m_output.Rrc.reg.addr.type = kind;
            m_output.Rrc.reg.addr.fid  = fid;
            m_output.Rrc.reg.addr.reg  = MAKE_REGADDR(type, reg);
            
            if (kind == RRT_LAST_SHARED) {
                // Register request
                m_output.Rrc.reg.value.m_state = RST_INVALID;
                m_output.Rrc.reg.return_addr   = m_output.Rc.index;
                m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
            } else {
                // Register send
                m_output.Rrc.reg.value = PipeValueToRegValue(type, m_input.Rbv);
            }
        }
        return true;        
    }
    
    // Local family
    const Family&          family = m_allocator.GetFamilyChecked(fid.lfid, fid.capability);
    const Family::RegInfo& regs   = family.regs[type];

    switch (kind)
    {
    case RRT_GLOBAL:
        // Writing a global
        if (reg >= regs.count.globals)
        {
            DebugProgWrite("Write attempt from %s (F%u/T%u) to global %u of F%u, limit %d",
                           GetKernel()->GetSymbolTable()[m_output.pc_dbg].c_str(), (unsigned)m_output.fid, (unsigned)m_output.tid, 
                           reg, (unsigned)fid.lfid, (int)regs.count.globals-1);
            break;
        }

        COMMIT
        {
            if (family.type == Family::LOCAL)
            {
                // Write into the global space
                m_output.Rc  = MAKE_REGADDR(type, regs.base + regs.size - regs.count.globals + reg);
                m_output.Rcv = m_input.Rbv;
            }
            else if (family.type == Family::GROUP)
            {
                // Send a network message
                m_output.Rrc.type = RemoteMessage::MSG_REGISTER;
                m_output.Rrc.reg.addr.type     = kind;
                m_output.Rrc.reg.addr.fid.pid  = INVALID_GPID;
                m_output.Rrc.reg.addr.fid.lfid = family.link_next;
                m_output.Rrc.reg.addr.reg      = MAKE_REGADDR(type, reg);
                m_output.Rrc.reg.value         = PipeValueToRegValue(type, m_input.Rbv);
            }
        }
        return true;
    
    case RRT_FIRST_DEPENDENT:
        // Writing the first dependent
        if (reg >= regs.count.shareds)
        {
            DebugProgWrite("Write attempt from %s (F%u/T%u) to shared %u of F%u, limit %d",
                           GetKernel()->GetSymbolTable()[m_output.pc_dbg].c_str(), (unsigned)m_output.fid, (unsigned)m_output.tid, 
                           reg, (unsigned)fid.lfid, (int)regs.count.shareds-1);
            break;
        }
    
        COMMIT
        {
            if (family.type == Family::LOCAL)
            {
                // Write to the family base, where the first dependents will be located
                m_output.Rc  = MAKE_REGADDR(type, regs.base + reg);
                m_output.Rcv = m_input.Rbv;
            }
            else if (family.type == Family::GROUP)
            {
                // Send a network message
                m_output.Rrc.type = RemoteMessage::MSG_REGISTER;
                m_output.Rrc.reg.addr.type     = kind;
                m_output.Rrc.reg.addr.fid.pid  = INVALID_GPID;
                m_output.Rrc.reg.addr.fid.lfid = family.link_next;
                m_output.Rrc.reg.addr.reg      = MAKE_REGADDR(type, reg);
                m_output.Rrc.reg.value         = PipeValueToRegValue(type, m_input.Rbv);
            }
        }
        return true;
    
    case RRT_LAST_SHARED:
        // Reading the last shared in the family
        if (reg >= regs.count.shareds)
        {
            DebugProgWrite("Read attempt from %s (F%u/T%u) from shared %u of F%u, limit %d",
                           GetKernel()->GetSymbolTable()[m_output.pc_dbg].c_str(), (unsigned)m_output.fid, (unsigned)m_output.tid, 
                           reg, (unsigned)fid.lfid, (int)regs.count.shareds-1);
            break;
        }
    
        COMMIT
        {
            if (family.type == Family::LOCAL || family.hasLastThread)
            {
                /*
                We cannot read the local register directly because we've already
                passed the Read Stage. Instead, we turn this into a network
                message as well. The Network logic will then read the register
                and write it back.
                */
                m_output.Rrc.type = RemoteMessage::MSG_REGISTER;
                m_output.Rrc.reg.addr.type     = kind;
                m_output.Rrc.reg.addr.fid.pid  = m_parent.GetProcessor().GetPID();
                m_output.Rrc.reg.addr.fid.lfid = fid.lfid;
                m_output.Rrc.reg.addr.reg      = MAKE_REGADDR(type, reg);
                m_output.Rrc.reg.return_addr   = m_output.Rc.index;
                m_output.Rrc.reg.value.m_state = RST_INVALID;

                m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
            }
            else if (family.type == Family::GROUP)
            {
                // Send a group network message
                m_output.Rrc.type = RemoteMessage::MSG_REGISTER;
                m_output.Rrc.reg.addr.type     = kind;
                m_output.Rrc.reg.addr.fid.pid  = INVALID_GPID;
                m_output.Rrc.reg.addr.fid.lfid = family.link_next;
                m_output.Rrc.reg.addr.reg      = MAKE_REGADDR(type, reg);
                m_output.Rrc.reg.return_addr   = m_output.Rc.index;
                m_output.Rrc.reg.value.m_state = RST_INVALID;
            
                m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
            }
        }
        return true;
    
    default:
        DebugProgWrite("Using an invalid family register");
        break;
    }
    
    COMMIT{ m_output.Rc = INVALID_REG; }
    return true;
}

bool Pipeline::ExecuteStage::ExecDetach(const FID& fid)
{
    /*
     Tell the writeback stage to send a detachment notification

     For local families we also make a network message, because detachments
     must follow shared reads (lest the detachment overtakes the shared reads
     and cleans up the target family).
    */
    if (fid.pid != m_parent.GetProcessor().GetPID())
    {
        COMMIT
        {
            m_output.Rrc.type = RemoteMessage::MSG_DETACH;
            m_output.Rrc.detach.fid = fid;
        }
    }
    else
    {
        Family& family = m_allocator.GetFamilyChecked(fid.lfid, fid.capability);
        switch (family.type)
        {
        case Family::GROUP:
            COMMIT
            {
                m_output.Rrc.type            = RemoteMessage::MSG_DETACH;
                m_output.Rrc.detach.fid.pid  = INVALID_GPID;
                m_output.Rrc.detach.fid.lfid = family.link_next;
            }
            break;
            
        case Family::LOCAL:
            COMMIT
            {
                m_output.Rrc.type       = RemoteMessage::MSG_DETACH;
                m_output.Rrc.detach.fid = fid;
            }
            break;
            
        default:
            assert(false);
            break;
        }
    }
    return true;
}

bool Pipeline::ExecuteStage::ExecSync(const FID& fid)
{
    assert(m_input.Rc.type == RT_INTEGER);
    
    if (fid.pid == m_parent.GetProcessor().GetPID())
    {
        // Local family
        Family& family = m_allocator.GetFamilyChecked(fid.lfid, fid.capability);
        COMMIT
        {
            if (family.sync.code != EXITCODE_NONE) {
                // The family is done, return the exit code
                m_output.Rcv.m_state = RST_FULL;
                m_output.Rcv.m_integer.set(family.sync.code, m_input.RcSize);
            } else if (m_input.Rc.index != INVALID_REG_INDEX) {
                // Buffer the request
                family.sync.pid = m_parent.GetProcessor().GetPID();
                family.sync.reg = m_input.Rc.index;
                m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
            }
        }
    }
    else
    {
        // Remote family
        COMMIT
        {
            if (m_input.Rc.index != INVALID_REG_INDEX)
            {
                m_output.Rrc.type     = RemoteMessage::MSG_SYNC;
                m_output.Rrc.sync.fid = fid;
                m_output.Rrc.sync.reg = m_input.Rc.index;
            
                m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
            }
        }
    }
    return true;
}

bool Pipeline::ExecuteStage::ExecAllocate(const PlaceID& place_, RegIndex reg)
{
    // Do some checking on the place
    PlaceID place(place_);
    switch (place.type)
    {
    case PLACE_DEFAULT:
        // Inherit the parent's place
        place.type = m_input.place;
        break;

    case PLACE_DELEGATE:
        // Verify processor ID
        if (place.pid >= m_parent.GetProcessor().GetGridSize())
        {
            throw SimulationException("Attempting to delegate to a non-existing core");
        }
        break;

    case PLACE_LOCAL:
    case PLACE_GROUP:
        // Nothing to do
        break;

    default:
        assert(false);
        break;
    }

    if (place.type == PLACE_DELEGATE)
    {
        // Send a network allocation request.
        // This will write back the FID to the specified register once the allocation
        // has completed.
        COMMIT
        {
            m_output.Rrc.type = RemoteMessage::MSG_ALLOCATE;
            m_output.Rrc.allocate.pid        = place.pid;
            m_output.Rrc.allocate.exclusive  = place.exclusive;
            m_output.Rrc.allocate.suspend    = place.suspend;
            m_output.Rrc.allocate.completion = reg;
            
            m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
        }
        return true;
    }
    
    // Local allocation
    FID fid;
    Result result = m_allocator.AllocateFamily(place, m_parent.GetProcessor().GetPID(), reg, &fid);
    if (result == FAILED)
    {
        return false;
    }
    
    COMMIT
    {
        if (result == SUCCESS)
        {
            m_output.Rcv.m_state   = RST_FULL;
            m_output.Rcv.m_integer = m_parent.GetProcessor().PackFID(fid);
        }
        else
        {
            assert(result == DELAYED);
            m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
        }
    }
    return true;
}

Pipeline::PipeAction Pipeline::ExecuteStage::ExecCreate(const FID& fid, MemAddr address, RegAddr completion)
{
    assert(completion.type == RT_INTEGER);

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
    
    COMMIT
    {
        m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
    }
    
    if (fid.pid == m_parent.GetProcessor().GetPID())
    {
        // Local create, queue it
        if (!m_allocator.QueueCreate(fid, address, completion.index))
        {
            return PIPE_STALL;
        }
    }
    else
    {
        // Delegated create; send the create -- no further processing required on this core
        // We send along the completion register. This will be returned with the delegation
        // result.
        COMMIT
        {
            m_output.Rrc.type = RemoteMessage::MSG_CREATE;
            m_output.Rrc.create.fid        = fid;
            m_output.Rrc.create.address    = address;
            m_output.Rrc.create.completion = completion.index;
        
            m_output.Rcv = MAKE_PENDING_PIPEVALUE(m_input.RcSize);
        }
    }
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::SetFamilyProperty(const FID& fid, FamilyProperty property, Integer value)
{
    if (fid.pid == m_parent.GetProcessor().GetPID())
    {
        // Local family
        Family& family = m_allocator.GetFamilyChecked(fid.lfid, fid.capability);
        COMMIT
        {
            switch (property)
            {
                case FAMPROP_START: family.start         = (SInteger)value; break;
                case FAMPROP_LIMIT: family.limit         = (SInteger)value; break;
                case FAMPROP_STEP:  family.step          = (SInteger)value; break;
                case FAMPROP_BLOCK: family.virtBlockSize = (TSize)value; break;
                default: assert(false); break;
            }
        }
    }
    else
    {
        // Remote family
        COMMIT
        {
            m_output.Rrc.type = RemoteMessage::MSG_SET_PROPERTY;
            m_output.Rrc.property.fid   = fid;
            m_output.Rrc.property.type  = property;
            m_output.Rrc.property.value = value;
        }
    }
    return PIPE_CONTINUE;
}

Pipeline::PipeAction Pipeline::ExecuteStage::ExecBreak()                    { return PIPE_CONTINUE; }
Pipeline::PipeAction Pipeline::ExecuteStage::ExecKill(const FID& /* fid */) { return PIPE_CONTINUE; }

void Pipeline::ExecuteStage::ExecDebugOutput(Integer value, int command, int flags) const
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
        DebugProgWrite("PRINT by T%u at %s: %s",
                       (unsigned)m_input.tid, GetKernel()->GetSymbolTable()[m_input.pc].c_str(),
                       stringout.str().c_str());
    }
}

void Pipeline::ExecuteStage::ExecOutputGraphics(Integer value, int command, int flags) const
{
    // command
    //  00 -> putpixel
    //  01 -> resize
    //  10 -> snapshot
    //std::cerr << "GFX: command " << command << " flags " << std::hex << flags << " value " << value << std::dec << std::endl;
    switch (command)
    {
    case 0:
    {
        // put pixel
        // flags: M - - - -
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
        if (flags & 0x10) {
            // position: offset(16) || offset(32)
            GetKernel()->GetDisplay().PutPixel(position, data);
            // std::cerr << "GFX ppfb: pos " << position << " data " << std::hex << data << std::dec << std::endl;
        } else {
            // position: x(8) y(8) || x(16) y(16)
            const unsigned int m = (1U << (size / 4)) - 1;
            const unsigned int y = (position >> (0 * size / 4)) & m;
            const unsigned int x = (position >> (1 * size / 4)) & m;
            GetKernel()->GetDisplay().PutPixel(x, y, data);
            // std::cerr << "GFX ppxy: x " << x << " y " << y << " data " << std::hex << data << std::dec << std::endl;
        }
        break;
    }
            
    case 1:
    {
        // resize screen
        // value:  W(8) H(8) unused(16)  ||  W(16) H(16) unused(32)
        const size_t       size = sizeof value * 8;
        const unsigned int mask = (1U << (size / 4)) - 1;
        const unsigned int w    = (value >> (3 * size / 4)) & mask;
        const unsigned int h    = (value >> (2 * size / 4)) & mask;
        GetKernel()->GetDisplay().Resize(w, h);
        break;
    }
        
    case 2:
    {
        // take screenshot
        // flags: T C - S S
        // T = embed timestamp in filename
        // C = embed thread information in picture comment
        // S = output stream
        //  0 -> file
        //  1 -> standard output
        //  2 -> standard error
        //  3 -> (undefined)
        // value: file identification key
        ostringstream tinfo;
        if (flags & 0x8)
        {
            tinfo << "print by thread 0x" 
                  << std::hex << (unsigned)m_input.tid 
                  << " at " << GetKernel()->GetSymbolTable()[m_input.pc]
                  << " on cycle " << std::dec << GetKernel()->GetCycleNo();
        }
            
        int outstream = flags & 3;
        if (outstream == 0)
        {
            ostringstream fname;
            fname << "gfx." << value;
            if (flags & 0x10)
            {
                fname << '.' << GetKernel()->GetCycleNo();
            }
            fname << ".ppm";
            ofstream f(fname.str().c_str(), ios_base::out | ios_base::trunc);
            GetKernel()->GetDisplay().Dump(f, value, tinfo.str());
        }
        else
        {
            ostream& out = (outstream == 2) ? cerr : cout;
            GetKernel()->GetDisplay().Dump(out, value, tinfo.str());
        }
        break;
    }
    }

}

void Pipeline::ExecuteStage::ExecStatusAction(Integer value, int command, int flags) const
{
    // command:
    //  00: status and continue
    //  01: status and fail
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
        DebugProgWrite("STATUS by T%u at %s: %s",
                       (unsigned)m_input.tid, GetKernel()->GetSymbolTable()[m_input.pc].c_str(),
                       stringout.str().c_str());
    }

    switch(command)
    {
    case 0: 
        break;
    case 1: 
        throw SimulationException("Interrupt requested by program.");
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

void Pipeline::ExecuteStage::ExecMemoryControl(Integer value, int command, int flags) const
{
    // command:
    //  00: mmap
    //  01: munmap
    // flags: L L L
    // size: 2^(L + 12)
    
    unsigned l = flags & 0x7;
    MemSize req_size = 1 << (l + 12);
    switch(command)
    {
    case 0:
        m_parent.GetProcessor().MapMemory(value, req_size);
        break;
    case 1:
        m_parent.GetProcessor().UnmapMemory(value, req_size);
        break;
    }    
}

void Pipeline::ExecuteStage::ExecDebug(Integer value, Integer stream) const
{
    // pattern: x x 1 x x - x x = graphics output

    // pattern: 0 0 1 - - - - - =   gfx: putpixel
    // pattern: 0 0 1 0 - - - - =     pixel at (x,y)
    // pattern: 0 0 1 1 - - - - =     pixel at offset

    // pattern: 0 1 1 - - - - - =   gfx: resize

    // pattern: 1 0 1 - - - - - =   gfx: screenshot
    // pattern: 1 0 1 0 - - - - =     without timestamp
    // pattern: 1 0 1 1 - - - - =     with timestamp
    // pattern: 1 0 1 - 0 - - - =     without thread info
    // pattern: 1 0 1 - 1 - - - =     with thread info
    // pattern: 1 0 1 - - - 0 0 =     output to file
    // pattern: 1 0 1 - - - 0 1 =     output to stdout
    // pattern: 1 0 1 - - - 1 0 =     output to stderr
    // pattern: 1 0 1 - - - 0 0 =     screenshot output: (unused)

    // pattern: 1 1 1 - - - - - =   gfx: (unused)

    // pattern: x x 0 1 x x x x = status and action
    // pattern: 0 0 0 1 - - - - =   status and continue
    // pattern: 0 1 0 1 - - - - =   status and fail
    // pattern: 1 0 0 1 - - - - =   status and abort
    // pattern: 1 1 0 1 - - - - =   status and exit with code
    // pattern: - - 0 1 - - 0 0 =   debug channel
    // pattern: - - 0 1 - - 0 1 =   stdout channel
    // pattern: - - 0 1 - - 1 0 =   stderr channel
    // pattern: - - 0 1 - - 0 0 =   channel: (unused)

    // pattern: x x 0 0 1 x x x = memory control
    // pattern: 0 0 0 0 1 L L L =   map
    // pattern: 0 1 0 0 1 L L L =   unmap

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
    if (stream & 0x20)
        ExecOutputGraphics(value, command, stream & 0x1B);
    else if ((stream & 0x30) == 0x10)
        ExecStatusAction(value, command, stream & 0xF);
    else if ((stream & 0x38) == 0x8)
        ExecMemoryControl(value, command, stream & 0x7);
    else if ((stream & 0x38) == 0)
        ExecDebugOutput(value, command, stream & 0x3);
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

Pipeline::ExecuteStage::ExecuteStage(Pipeline& parent, const ReadExecuteLatch& input, ExecuteMemoryLatch& output, Allocator& alloc, FamilyTable& familyTable, ThreadTable& threadTable, FPU& fpu, size_t fpu_source, const Config& /*config*/)
  : Stage("execute", parent),
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
}

}
