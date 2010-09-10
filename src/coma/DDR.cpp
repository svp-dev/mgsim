#include "DDR.h"
#include "VirtualMemory.h"
#include "../config.h"
#include "../log2.h"
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
    
    if (!m_busy.Set())
    {
        return false;
    }
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
    
    if (!m_busy.Set())
    {
        return false;
    }
    return true;
}

// Main process for timing the current active request
Result DDRChannel::DoRequest()
{
    assert(m_busy.IsSet());
    
    const CycleNo now = m_clock.GetCycleNo();
    if (now < m_next_command)
    {
        // Can't continue yet
        return SUCCESS;
    }
    
    // We read from m_nDevicesPerRank devices, each providing m_nBurstLength bytes in the burst.
    const unsigned burst_size = m_ddrconfig.m_nBurstSize;
    
    // Decode the burst address and offset-within-burst
    const MemAddr      address = (m_request.address + m_request.offset) / burst_size;
    const unsigned int offset  = (m_request.address + m_request.offset) % burst_size;
    const unsigned int rank    = GET_BITS(address, m_ddrconfig.m_nRankStart, m_ddrconfig.m_nRankBits),
                       row     = GET_BITS(address, m_ddrconfig.m_nRowStart,  m_ddrconfig.m_nRowBits);

    if (m_currentRow[rank] != row)
    {
        if (m_currentRow[rank] != INVALID_ROW)
        {
            // Precharge (close) the currently active row
            COMMIT
            {
                m_next_command = std::max(m_next_precharge, now) + m_ddrconfig.m_tRP;
                m_currentRow[rank] = INVALID_ROW;
            }
            return SUCCESS;
        }
        
        // Activate (open) the desired row
        COMMIT
        {
            m_next_command   = now + m_ddrconfig.m_tRCD;
            m_next_precharge = now + m_ddrconfig.m_tRAS;
            
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

            m_next_command   = now + m_ddrconfig.m_tCWL;
            m_next_precharge = now + m_ddrconfig.m_tWR;
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
            m_request.done    = now + m_ddrconfig.m_tCL;
            
            // Schedule next read
            m_next_command = now + m_ddrconfig.m_tCCD;
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
    if (!m_busy.Clear())
    {
        return FAILED;
    }
    return SUCCESS;
}

Result DDRChannel::DoPipeline()
{
    assert(!m_pipeline.Empty());
    const CycleNo  now     = m_clock.GetCycleNo();
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

DDRChannel::DDRConfig::DDRConfig(const Clock& clock, const Config& config)
{
    // DDR 3
    m_nBurstLength = config.getInteger<size_t> ("DDR_BurstLength", 8);
    if (m_nBurstLength != 8)
        throw SimulationException("This implementation only supports m_nBurstLength = 8");
    size_t cellsize = config.getInteger<size_t> ("DDR_CellSize", 8);
    if (cellsize != 8)
        throw SimulationException("This implementation only supports DDR_CellSize = 8");

    // Default values for DDR3-1600 (200 MHz clock).
    // Configuration based on the Micron MT41J128M8.
    // Latencies in DDR specs are expressed in tCK (I/O cycles).
    m_tCL  = config.getInteger<unsigned> ("DDR_tCL",  11);
    m_tRCD = config.getInteger<unsigned> ("DDR_tRCD", 11);
    m_tRP  = config.getInteger<unsigned> ("DDR_tRP",  11);
    m_tRAS = config.getInteger<unsigned> ("DDR_tRAS", 28);
    m_tCWL = config.getInteger<unsigned> ("DDR_tCWL",  8);
    m_tCCD = config.getInteger<unsigned> ("DDR_tCCD",  4);
    
    // tWR is expressed in DDR specs in nanoseconds, see 
    // http://www.samsung.com/global/business/semiconductor/products/dram/downloads/applicationnote/tWR.pdf
    // Frequency is in MHz.
    m_tWR = config.getInteger<unsigned> ("DDR_tWR", 15) / 1e3 * clock.GetFrequency();

    // Address bit mapping.
    // One row bit added for a 4GB DIMM with ECC.
    m_nDevicesPerRank = config.getInteger<size_t> ("DDR_DevicesPerRank", 8);
    m_nRankBits = ilog2(config.getInteger<size_t> ("DDR_Ranks", 2));
    m_nRowBits = config.getInteger<size_t> ("DDR_RowBits", 15);
    m_nColumnBits = config.getInteger<size_t> ("DDR_ColumnBits", 10);
    m_nBurstSize = m_nDevicesPerRank * m_nBurstLength;

    // ordering of bits in address:
    m_nColumnStart = 0;
    m_nRowStart = m_nColumnStart + m_nColumnBits;
    m_nRankStart = m_nRowStart + m_nRowBits;
}

DDRChannel::DDRChannel(const std::string& name, Object& parent, Clock& clock, VirtualMemory& memory, const Config& config)
    : Object(name, parent, clock),
      m_clock(clock),
      m_ddrconfig(clock, config),
      // Initialize each rank at 'no row selected'
      m_currentRow(1 << m_ddrconfig.m_nRankBits, INVALID_ROW),

      m_memory(memory),
      m_callback(dynamic_cast<ICallback&>(parent)),
      m_pipeline(clock, m_ddrconfig.m_tCL),
      m_busy(clock, false),
      m_next_command(0),
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


