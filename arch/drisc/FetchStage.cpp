#include "DRISC.h"
#include <sim/config.h>
#include <sim/breakpoints.h>

#include <cassert>
using namespace std;

namespace Simulator
{

void DRISC::Pipeline::FetchStage::Clear(TID tid)
{
    if (m_output.tid == tid)
    {
        m_switched = true;
    }
}

DRISC::Pipeline::PipeAction DRISC::Pipeline::FetchStage::OnCycle()
{
    MemAddr pc = m_pc;
    if (m_switched)
    {
        // We need to switch to a new thread

        // Get the thread on the front of the active queue
        TID tid = m_allocator.PopActiveThread();
        if (tid == INVALID_TID)
        {
            // Nothing to do....
            return PIPE_IDLE;
        }

        // Read its information from the Family and Thread Tables
        auto& thread = m_threadTable[tid];
        auto& family = m_familyTable[thread.family];

        pc = thread.pc;
        if (!family.legacy && pc % m_controlBlockSize == 0)
        {
            // We need to check for breakpoints on the control
            // word here.
            GetKernel()->GetBreakPointManager().Check(BreakPointManager::FETCH, pc, *this);

            // Skip the control word
            pc += sizeof(Instruction);
        }

        // Read the cache line for this PC
        size_t offset = (size_t)(pc % m_icache.GetLineSize());   // Offset within the cacheline
        if (!m_icache.Read(thread.cid, pc - offset, m_buffer, m_icache.GetLineSize()))
        {
            DeadlockWrite("F%u/T%u(%llu) %s fetch stall due to I-cache miss",
                          (unsigned)thread.family, (unsigned)tid, (unsigned long long)thread.index,
                          GetDRISC().GetSymbolTable()[pc].c_str());
            return PIPE_STALL;
        }

        COMMIT
        {
            m_output.tid       = tid;
            m_output.fid       = thread.family;
            m_output.legacy    = family.legacy;
            m_output.placeSize = family.placeSize;

            m_output.logical_index = thread.index; // for traces only

            for (size_t i = 0; i < NUM_REG_TYPES; ++i)
            {
                m_output.regs.types[i].family = family.regs[i];
                m_output.regs.types[i].thread = thread.regs[i];
            }

            // Mark the thread as running
            thread.state = TST_RUNNING;
        }

        DebugSimWrite("F%u/T%u(%llu) %s switched in",
                      (unsigned)thread.family, (unsigned)tid, (unsigned long long)thread.index,
                      GetDRISC().GetSymbolTable()[pc].c_str());
    }

    COMMIT
    {
        // Read the instruction and control bits
        const size_t offset   = (size_t)(pc % m_icache.GetLineSize());                // Offset within the cacheline
        const size_t iInstr   = offset / sizeof(Instruction);                         // Offset in instructions
        const size_t iControl = (offset & -m_controlBlockSize) / sizeof(Instruction); // Align offset down to control block size

        const Instruction* instrs = (const Instruction*)m_buffer;
        const Instruction control = (!m_output.legacy) ? UnserializeInstruction(&instrs[iControl]) >> (2 * (iInstr - iControl)) : 0;
        const MemAddr     next_pc = pc + sizeof(Instruction);

        // Handle thread switching. This is a combination of the following factors:
        // 1) a switch annotation in the instruction stream: the thread itself "wants" to switch
        // 2) there is only one thread in the active queue, so a desired switch should be ignored
        // 3) the thread has terminated or current I-cache line is exhausted, in which case switch must happen

        // 1) swch annotation:
        const bool wantSwitch = ((control & 1) != 0);

        // 2) only one thread: here we have to be careful about the queue semantics. We distinguish two cases:
        // - the thread was popped earlier than the current cycle, in which case Empty() is true
        // - the thread was popped *in this cycle*, in which case Empty() is *not yet* true, because the update to
        // the storage is only processed at the start of the next cycle. So we also check if there is only 1 thread (Singular)
        // and the current thread is the only thread at the front.
        auto& activeThreads = m_allocator.m_activeThreads;
        const bool lastThread = activeThreads.Empty() || (activeThreads.Singular() && activeThreads.Front() == m_output.tid);

        // 3) terminated or i-cache boundary
        m_output.kill         = ((control & 2) != 0);
        const bool mustSwitch = m_output.kill || (next_pc % m_icache.GetLineSize() == 0);

        // Switch if must, or if desired unless there is only 1 thread:
        m_output.swch         = mustSwitch || (wantSwitch && !lastThread);

        // Fill output latch structure
        m_output.pc           = pc;
        m_output.instr        = UnserializeInstruction(&instrs[iInstr]);

        m_output.pc_dbg       = pc;
        if (GetKernel()->GetDebugMode() & Kernel::DEBUG_CPU_MASK)
        {
            m_output.pc_sym = GetDRISC().GetSymbolTable()[m_output.pc].c_str();
        }
        else
        {
            m_output.pc_sym = "(untranslated)";
        }

        // Check for breakpoints
        GetKernel()->GetBreakPointManager().Check(BreakPointManager::FETCH, pc, *this);

        // Update the PC and switched state
        m_pc       = next_pc;
        m_switched = m_output.swch;
    }

    DebugPipeWrite("F%u/T%u(%llu) %s fetched 0x%.*lx (switching: %s)",
                   (unsigned)m_output.fid, (unsigned)m_output.tid, (unsigned long long)m_output.logical_index, m_output.pc_sym,
                   (int)(sizeof(Instruction) * 2), (unsigned long)m_output.instr,
                   m_switched ? "yes" : "no");

    return PIPE_CONTINUE;
}

DRISC::Pipeline::FetchStage::FetchStage(Pipeline& parent, Clock& clock,
                                        FetchDecodeLatch& output,
                                        Allocator& alloc,
                                        drisc::FamilyTable& familyTable,
                                        drisc::ThreadTable& threadTable,
                                        drisc::ICache& icache,
                                        Config& config)
  : Stage("fetch", parent, clock),
    m_output(output),
    m_allocator(alloc),
    m_familyTable(familyTable),
    m_threadTable(threadTable),
    m_icache(icache),
    m_controlBlockSize(config.getValue<size_t>("ControlBlockSize")),
    m_buffer(0),
    m_switched(true),
    m_pc(0)
{
    if ((m_controlBlockSize & ~(m_controlBlockSize - 1)) != m_controlBlockSize)
    {
        throw InvalidArgumentException("Control block size is not a power of two");
    }

    if (m_controlBlockSize > (sizeof(Instruction)*sizeof(Instruction)*8/2))
    {
        throw InvalidArgumentException("Control block size causes control word to be larger than an instruction");
    }

    if (m_controlBlockSize > m_icache.GetLineSize())
    {
        throw InvalidArgumentException("Control block size is larger than the cache line size");
    }

    m_buffer = new char[m_icache.GetLineSize()];
}

DRISC::Pipeline::FetchStage::~FetchStage()
{
    delete[] m_buffer;
}

}
