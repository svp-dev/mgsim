#include "DDR.h"
#include "VirtualMemory.h"
#include <limits>
#include <cstdio>

namespace Simulator
{

template <typename T>
static T GET_BITS(const T& value, unsigned int offset, unsigned int size)
{
    return (value >> offset) & ((T(1) << size) - 1);
}

static const unsigned long INVALID_ROW = std::numeric_limits<unsigned long>::max();

bool DDRChannel::Read(MemAddr address, MemSize size)
{
    if (m_busy.IsSet())
    {
        // We're still busy
        return false;
    }
    
    // Accept request
    COMMIT
    {
        m_request.address   = address;
        m_request.offset    = 0;
        m_request.data.size = size;
        m_request.write     = false;     
        m_next_command = 0;
    }

    // Get the actual data
    m_memory.Read(address, m_request.data.data, size);
    
    m_busy.Set();
    return true;
}

bool DDRChannel::Write(MemAddr address, const void* data, MemSize size)
{
    if (m_busy.IsSet())
    {
        // We're still busy
        return false;
    }
    
    // Accept request
    COMMIT
    {
        m_request.address   = address;
        m_request.offset    = 0;
        m_request.data.size = size;
        m_request.write     = true;
        
        m_next_command = 0;
    }

    // Write the actual data
    m_memory.Write(address, data, size);
    
    m_busy.Set();
    return true;
}

// Main process for timing the current active request
Result DDRChannel::DoRequest()
{
    assert(m_busy.IsSet());
    
    const CycleNo now = GetKernel()->GetCycleNo();
    if (now < m_next_command)
    {
        // Can't continue yet
        return SUCCESS;
    }
    
    // We read from m_nDevicePerRank devices, each providing m_nBurstLength bytes in the burst.
    const unsigned int burst_size = (m_nDevicePerRank * m_nBurstLength);
    
    // Decode the burst address and offset-within-burst
    const MemAddr      address = (m_request.address + m_request.offset) / burst_size;
    const unsigned int offset  = (m_request.address + m_request.offset) % burst_size;
    const unsigned int rank    = GET_BITS(address, m_nRankStart, m_nRankBits),
                       row     = GET_BITS(address, m_nRowStart,  m_nRowBits);

    if (m_currentRow[rank] != row)
    {
        if (m_currentRow[rank] != INVALID_ROW)
        {
            // Precharge (close) the currently active row
            COMMIT
            {
                m_next_command = /* std::max(m_next_precharge, now) */ now  + m_tRP;
                m_currentRow[rank] = INVALID_ROW;
            }
            return SUCCESS;
        }
        
        // Activate (open) the desired row
        COMMIT
        {
            m_next_command   = now + m_tRCD;
            m_next_precharge = now + m_tRAS;
            
            m_currentRow[rank] = row;
        }
        return SUCCESS;
    }
    
    // Process a single burst
    unsigned int remainder = m_request.data.size - m_request.offset;
    unsigned int size      = std::min(burst_size - offset, remainder);

    if (m_request.write)
    {
        COMMIT
        {
            // Update address to reflect written portion
            m_request.offset += size;

            m_next_command   = now + m_tCWL;
            m_next_precharge = now + m_tWR;
        }

        if (size < remainder)
        {
            // We're not done yet
            return SUCCESS;
        }
    }
    else
    {
        COMMIT
        {
            // Update address to reflect read portion
            m_request.offset += size;
            m_request.done    = now + m_tCL;
            
            // Schedule next read
            m_next_command = now + m_tCCD;
        }
    
        if (size < remainder)
        {
            // We're not done yet
            return SUCCESS;
        }
        
        // We're done with this read; queue it into the pipeline
        if (!m_pipeline.Push(m_request))
        {
            // The read pipeline should be big enough
            assert(false);
            return FAILED;
        }
    }

    // We've completed this request
    m_busy.Clear();
    return SUCCESS;
}

Result DDRChannel::DoPipeline()
{
    assert(!m_pipeline.Empty());
    const CycleNo  now     = GetKernel()->GetCycleNo();
    const Request& request = m_pipeline.Front();
    if (request.done >= now)
    {
        // The last burst has completed, send the assembled data back
        assert(!request.write);
        if (!m_callback.OnReadCompleted(request.address, request.data))
        {
            return FAILED;
        }
        m_pipeline.Pop();
    }
    return SUCCESS;
}

DDRChannel::DDRChannel(const std::string& name, Object& parent, VirtualMemory& memory)
  : Object(name, parent),

    // DDR 3
    m_tBusMult(4),
    m_nBurstLength(m_tBusMult * 2),
    
    // Default values for DDR3-1600 (200 MHz clock).
    // Latencies expressed in tCK (cycle) for a 800 MHz I/O clock.
    // Configuration based on the Micron MT41J128M8.
    /*m_tRCD(11),
    m_tRP(11),
    m_tCL(11),
    m_tWR(8),
    m_tCCD(4),
    m_tCWL(8),
    m_tRAS(28),*/
    
    m_tRCD(22),
    m_tRP(22),
    m_tCL(22),
    m_tWR(16),
    m_tCCD(8),
    m_tCWL(16),
    m_tRAS(56),
    
    // Address bit mapping.
    // One row bit added for a 4GB DIMM.
    m_nDevicePerRank(8),
    m_nRankBits(1),
    m_nRowBits(18),
    m_nColumnBits(10),
    m_nRankStart(28),
    m_nRowStart(10),
    m_nColumnStart(0),

    // Initialize each rank at 'no row selected'
    m_currentRow(1 << m_nRankBits, INVALID_ROW),
    
    m_memory(memory),
    m_callback(dynamic_cast<ICallback&>(parent)),
    
    m_pipeline(*parent.GetKernel(), m_tCL),
    m_busy(*parent.GetKernel(), false),
    m_next_precharge(0),
    
    p_Request ("request",  delegate::create<DDRChannel, &DDRChannel::DoRequest >(*this)),
    p_Pipeline("pipeline", delegate::create<DDRChannel, &DDRChannel::DoPipeline>(*this))
{
    m_busy.Sensitive(p_Request);
    m_pipeline.Sensitive(p_Pipeline);
}
                                                                                                                                                                                
DDRChannel::~DDRChannel()
{
}

}


