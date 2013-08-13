#include "DRISC.h"
#include <sim/breakpoints.h>
#include <sim/sampling.h>

#include <cassert>
#include <sstream>
#include <iomanip>

using namespace std;

namespace Simulator
{

DRISC::Pipeline::PipeAction DRISC::Pipeline::MemoryStage::OnCycle()
{
    PipeValue rcv = m_input.Rcv;

    unsigned inload = 0;
    unsigned instore = 0;

    if (m_input.size > 0)
    {
        // It's a new memory operation!
        assert(m_input.size <= sizeof(uint64_t));

        Result result = SUCCESS;
        if (rcv.m_state == RST_FULL)
        {
            // Memory write
            try
            {

                // Check for breakpoints
                GetKernel()->GetBreakPointManager().Check(BreakPointManager::MEMWRITE, m_input.address, *this);

                // Serialize and store data
                char data[MAX_MEMORY_OPERATION_SIZE];

                uint64_t value = 0;
                switch (m_input.Rc.type) {
                case RT_INTEGER: value = m_input.Rcv.m_integer.get(m_input.Rcv.m_size); break;
                case RT_FLOAT:   value = m_input.Rcv.m_float.toint(m_input.Rcv.m_size); break;
                default: UNREACHABLE;
                }



                SerializeRegister(m_input.Rc.type, value, data, (size_t)m_input.size);

                auto& mmio = m_parent.GetDRISC().GetIOMatchUnit();
                if (mmio.IsRegisteredWriteAddress(m_input.address, m_input.size))
                {
                    result = mmio.Write(m_input.address, data, m_input.size, m_input.fid, m_input.tid);

                    if (result == FAILED)
                    {
                        DeadlockWrite("F%u/T%u(%llu) %s stall (I/O store *%#.*llx/%zd <- %s)",
                                      (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                                      m_input.pc_sym,
                                      (int)(sizeof(MemAddr)*2), (unsigned long long)m_input.address, (size_t)m_input.size,
                                      m_input.Rcv.str(m_input.Rc.type).c_str());

                        return PIPE_STALL;
                    }
                }
                else
                {
                    // Normal request to memory
                    if ((result = m_dcache.Write(m_input.address, data, m_input.size, m_input.fid, m_input.tid)) == FAILED)
                    {
                        // Stall
                        DeadlockWrite("F%u/T%u(%llu) %s stall (L1 store *%#.*llx/%zd <- %s)",
                                      (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                                      m_input.pc_sym,
                                      (int)(sizeof(MemAddr)*2), (unsigned long long)m_input.address, (size_t)m_input.size,
                                      m_input.Rcv.str(m_input.Rc.type).c_str());

                        return PIPE_STALL;
                    }

                    if (!m_allocator.IncreaseThreadDependency(m_input.tid, THREADDEP_OUTSTANDING_WRITES))
                    {
                        DeadlockWrite("F%u/T%u(%llu) %s unable to increase OUTSTANDING_WRITES",
                                      (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                                      m_input.pc_sym);
                        return PIPE_STALL;
                    }
                }

                // Clear the register state so it won't get written to the register file
                rcv.m_state = RST_INVALID;

                // Prepare for count increment
                instore = m_input.size;

                DebugMemWrite("F%u/T%u(%llu) %s store *%#.*llx/%zd <- %s %s",
                              (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                              m_input.pc_sym,
                              (int)(sizeof(MemAddr)*2),
                              (unsigned long long)m_input.address, (size_t)m_input.size,
                              m_input.Ra.str().c_str(), m_input.Rcv.str(m_input.Ra.type).c_str());
            }
            catch (SimulationException& e)
            {
                // Add details about store
                stringstream details;
                details << "While processing store: *"
                        << setw(sizeof(Integer) * 2) << setfill('0') << right << hex << m_input.address << left
                        << " <- " << m_input.Ra.str() << " = " << m_input.Rcv.str(m_input.Ra.type);
                e.AddDetails(details.str());
                throw;
            }
        }
        // Memory read
        else if (m_input.Rc.valid())
        {
            // Check for breakpoints
            GetKernel()->GetBreakPointManager().Check(BreakPointManager::MEMREAD, m_input.address, *this);

            if (m_input.address >= 4 && m_input.address < 8)
            {
                // Special range. Rather hackish.
                // Note that we exclude address 0 from this so NULL pointers are still invalid.

                // Invalid address; don't send request, just clear register
                rcv = MAKE_EMPTY_PIPEVALUE(rcv.m_size);

                DebugMemWrite("F%u/T%u(%llu) %s clear %s",
                              (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                              m_input.pc_sym,
                              m_input.Rc.str().c_str());
            }
            else
            {
                // regular read from L1

                try
                {
                    char data[MAX_MEMORY_OPERATION_SIZE];
                    RegAddr reg = m_input.Rc;

                    auto& mmio = m_parent.GetDRISC().GetIOMatchUnit();
                    if (mmio.IsRegisteredReadAddress(m_input.address, m_input.size))
                    {
                        result = mmio.Read(m_input.address, data, m_input.size, m_input.fid, m_input.tid, m_input.Rc);

                        switch(result)
                        {
                        case FAILED:
                            DeadlockWrite("F%u/T%u(%llu) %s stall (I/O load *%#.*llx/%zu bytes -> %s)",
                                          (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                                          m_input.pc_sym,
                                          (int)(sizeof(MemAddr)*2), (unsigned long long)m_input.address, (size_t)m_input.size,
                                          m_input.Rc.str().c_str());

                            return PIPE_STALL;

                        case DELAYED:
                            rcv = MAKE_PENDING_PIPEVALUE(rcv.m_size);
                            rcv.m_memory.fid         = m_input.fid;
                            rcv.m_memory.next        = INVALID_REG;
                            rcv.m_memory.offset      = 0;
                            rcv.m_memory.size        = (size_t)m_input.size;
                            rcv.m_memory.sign_extend = m_input.sign_extend;

                            // Increase the outstanding memory count for the family
                            if (!m_allocator.OnMemoryRead(m_input.fid))
                            {
                                return PIPE_STALL;
                            }

                            DebugMemWrite("F%u/T%u(%llu) %s I/O load *%#.*llx/%zu -> delayed %s",
                                          (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                                          m_input.pc_sym,
                                          (int)(sizeof(MemAddr)*2), (unsigned long long)m_input.address, (size_t)m_input.size,
                                          m_input.Rc.str().c_str());

                            break;

                        case SUCCESS:
                            break;
                        }
                    }
                    else
                    {
                        // Normal read from memory.
                        result = m_dcache.Read(m_input.address, data, m_input.size, &reg);

                        switch(result)
                        {
                        case FAILED:
                            // Stall
                            DeadlockWrite("F%u/T%u(%llu) %s stall (L1 load *%#.*llx/%zu bytes -> %s)",
                                          (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                                          m_input.pc_sym,
                                          (int)(sizeof(MemAddr)*2), (unsigned long long)m_input.address, (size_t)m_input.size,
                                          m_input.Rc.str().c_str());

                            return PIPE_STALL;

                        case DELAYED:

                            // Remember request data
                            rcv = MAKE_PENDING_PIPEVALUE(rcv.m_size);
                            rcv.m_memory.fid         = m_input.fid;
                            rcv.m_memory.next        = reg;
                            rcv.m_memory.offset      = (unsigned int)(m_input.address % m_dcache.GetLineSize());
                            rcv.m_memory.size        = (size_t)m_input.size;
                            rcv.m_memory.sign_extend = m_input.sign_extend;

                            // Increase the outstanding memory count for the family
                            if (!m_allocator.OnMemoryRead(m_input.fid))
                            {
                                return PIPE_STALL;
                            }


                            DebugMemWrite("F%u/T%u(%llu) %s L1 load *%#.*llx/%zu -> delayed %s",
                                          (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                                          m_input.pc_sym,
                                          (int)(sizeof(MemAddr)*2), (unsigned long long)m_input.address, (size_t)m_input.size,
                                          m_input.Rc.str().c_str());

                            break;
                        case SUCCESS:
                            break;
                        }
                    }

                    // Prepare for counter increment
                    inload = m_input.size;

                    rcv.m_size = m_input.Rcv.m_size;
                    if (result == SUCCESS)
                    {
                        // Unserialize and store data
                        uint64_t value = UnserializeRegister(m_input.Rc.type, data, (size_t)m_input.size);

                        if (m_input.sign_extend)
                        {
                            // Sign-extend the value
                            size_t shift = (sizeof(value) - (size_t)m_input.size) * 8;
                            value = (int64_t)(value << shift) >> shift;
                        }

                        rcv.m_state = RST_FULL;
                        switch (m_input.Rc.type)
                        {
                        case RT_INTEGER: rcv.m_integer.set(value, rcv.m_size); break;
                        case RT_FLOAT:   rcv.m_float.fromint(value, rcv.m_size); break;
                        default:         UNREACHABLE;
                        }

                        // Memory read
                        DebugMemWrite("F%u/T%u(%llu) %s load *%#.*llx/%zu -> %s %s",
                                      (unsigned)m_input.fid, (unsigned)m_input.tid, (unsigned long long)m_input.logical_index,
                                      m_input.pc_sym,
                                      (int)(sizeof(MemAddr)*2), (unsigned long long)m_input.address, (size_t)m_input.size,
                                      m_input.Rc.str().c_str(), rcv.str(m_input.Rc.type).c_str());
                    }
                }
                catch (SimulationException& e)
                {
                    // Add details about load
                    stringstream details;
                    details << "While processing load: *"
                            << setw(sizeof(Integer) * 2) << setfill('0') << right << hex << m_input.address << left
                            << " -> " << m_input.Rc.str();
                    e.AddDetails(details.str());
                    throw;
                }
            }
        }
    }

    COMMIT
    {
        // Copy common latch data
        (CommonData&)m_output = m_input;

        m_output.suspend = m_input.suspend;
        m_output.Rc      = m_input.Rc;
        m_output.Rrc     = m_input.Rrc;
        m_output.Rcv     = rcv;

        // Increment counters
        m_loads += !!inload;
        m_load_bytes += inload;
        m_stores += !!instore;
        m_store_bytes += instore;
    }
    return PIPE_CONTINUE;
}

DRISC::Pipeline::MemoryStage::MemoryStage(Pipeline& parent, Clock& clock, const ExecuteMemoryLatch& input, MemoryWritebackLatch& output, DCache& dcache, Allocator& alloc, Config& /*config*/)
    : Stage("memory", parent, clock),
      m_input(input),
      m_output(output),
      m_allocator(alloc),
      m_dcache(dcache),
      m_loads(0),
      m_stores(0),
      m_load_bytes(0),
      m_store_bytes(0)
{
    RegisterSampleVariableInObject(m_loads, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_stores, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_load_bytes, SVC_CUMULATIVE);
    RegisterSampleVariableInObject(m_store_bytes, SVC_CUMULATIVE);
}

}
