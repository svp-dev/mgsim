#include "DDR.h"
#include <sim/config.h>
#include <sim/log2.h>

#include <sstream>
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
        m_request.address = address;
        m_request.offset  = 0;
        m_request.size    = size;
        m_request.write   = false;
    }
    
    if (!m_busy.Set())
    {
        return false;
    }
    return true;
}

bool DDRChannel::Write(MemAddr address, MemSize size)
{
    if (m_busy.IsSet())
    {
        // We're still busy
        return false;
    }
    
    // Accept request
    COMMIT
    {
        m_request.address = address;
        m_request.offset  = 0;
        m_request.size    = size;
        m_request.write   = true;
    }

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
    
    const CycleNo now = GetClock().GetCycleNo();
    if (now < m_next_command)
    {
        // Can't continue yet
        return SUCCESS;
    }
    
    // We read from m_nDevicesPerRank devices, each providing m_nBurstLength bytes in the burst.
    const unsigned burst_size = m_ddrconfig.m_nBurstSize;
    
    // Decode the burst address and offset-within-burst
    const MemAddr      address = (m_request.address + m_request.offset) / m_ddrconfig.m_nDevicesPerRank;
    const unsigned int offset  = (m_request.address + m_request.offset) % m_ddrconfig.m_nDevicesPerRank;
    const unsigned int bank    = GET_BITS(address, m_ddrconfig.m_nBankStart, m_ddrconfig.m_nBankBits),
                       rank    = GET_BITS(address, m_ddrconfig.m_nRankStart, m_ddrconfig.m_nRankBits),
                       row     = GET_BITS(address, m_ddrconfig.m_nRowStart,  m_ddrconfig.m_nRowBits);

    // Ranks and banks are analogous in this concept; each bank can be invidually pre-charged and activated,
    // providing an array of rows * columns cells.
    const unsigned int array  = rank * (1 << m_ddrconfig.m_nBankBits) + bank;
    
    if (m_currentRow[array] != row)
    {
        if (m_currentRow[array] != INVALID_ROW)
        {
            // Precharge (close) the currently active row
            COMMIT
            {
                m_next_command = std::max(m_next_precharge, now) + m_ddrconfig.m_tRP;
                m_currentRow[array] = INVALID_ROW;
            }
            return SUCCESS;
        }
        
        // Activate (open) the desired row
        COMMIT
        {
            m_next_command   = now + m_ddrconfig.m_tRCD;
            m_next_precharge = now + m_ddrconfig.m_tRAS;
            
            m_currentRow[array] = row;
        }
        return SUCCESS;
    }
    
    // Process a single burst
    unsigned int remainder = m_request.size - m_request.offset;
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
    const CycleNo  now     = GetClock().GetCycleNo();
    const Request& request = m_pipeline.Front();
    if (now >= request.done)
    {
        // The last burst has completed, send the assembled data back
        assert(!request.write);
        if (!m_callback->OnReadCompleted())
        {
            return FAILED;
        }
        m_pipeline.Pop();
    }
    COMMIT{ m_busyCycles++; }
    return SUCCESS;
}

DDRChannel::DDRConfig::DDRConfig(const std::string& name, Object& parent, Clock& clock, Config& config)
    : Object(name, parent, clock),
      m_nBurstLength(config.getValue<size_t> (*this, "BurstLength")),
      m_tRCD (config.getValue<unsigned> (*this, "tRCD")),
      m_tRP  (config.getValue<unsigned> (*this, "tRP")),
      m_tCL  (config.getValue<unsigned> (*this, "tCL")),
      // tWR is expressed in DDR specs in nanoseconds, see
      // http://www.samsung.com/global/business/semiconductor/products/dram/downloads/applicationnote/tWR.pdf
      // Frequency is in MHz.
      m_tWR  (config.getValue<unsigned> (*this, "tWR") / 1e3 * clock.GetFrequency()),

      m_tCCD (config.getValue<unsigned> (*this, "tCCD")),
      m_tCWL (config.getValue<unsigned> (*this, "tCWL")),
      m_tRAS (config.getValue<unsigned> (*this, "tRAS")),

      // Address bit mapping.
      m_nDevicesPerRank (config.getValue<size_t> (*this, "DevicesPerRank")),
      m_nBankBits (ilog2(config.getValue<size_t> (*this, "Banks"))),
      m_nRankBits (ilog2(config.getValue<size_t> (*this, "Ranks"))),
      m_nRowBits (config.getValue<size_t> (*this, "RowBits")),
      m_nColumnBits (config.getValue<size_t> (*this, "ColumnBits")),

      m_nColumnStart (0),
      m_nBankStart (m_nColumnStart + m_nColumnBits),
      m_nRankStart (m_nBankStart + m_nBankBits),
      m_nRowStart (m_nRankStart + m_nRankBits),

      m_nBurstSize (m_nDevicesPerRank * m_nBurstLength)
{
    if (m_nBurstLength != 8)
        throw SimulationException(*this, "This implementation only supports m_nBurstLength = 8");
        
    size_t cellsize = config.getValue<size_t> (*this, "CellSize");
    if (cellsize != 8)
        throw SimulationException(*this, "This implementation only supports CellSize = 8");
}

DDRChannel::DDRChannel(const std::string& name, Object& parent, Clock& clock, Config& config)
    : Object(name, parent, clock),
      m_registry(config),
      m_ddrconfig("config", *this, clock, config),
      // Initialize each rank at 'no row selected'
      m_currentRow(1 << (m_ddrconfig.m_nRankBits + m_ddrconfig.m_nBankBits), INVALID_ROW),

      m_callback(0),
      m_pipeline("b_pipeline", *this, clock, m_ddrconfig.m_tCL),
      m_busy("f_busy", *this, clock, false),
      m_next_command(0),
      m_next_precharge(0),
    
      p_Request (*this, "request",  delegate::create<DDRChannel, &DDRChannel::DoRequest >(*this)),
      p_Pipeline(*this, "pipeline", delegate::create<DDRChannel, &DDRChannel::DoPipeline>(*this)),
      
      m_busyCycles(0)
{
    m_busy.Sensitive(p_Request);
    m_pipeline.Sensitive(p_Pipeline);

    config.registerObject(*this, "ddr");
    config.registerProperty(*this, "CL", (uint32_t)m_ddrconfig.m_tCL);
    config.registerProperty(*this, "RCD", (uint32_t)m_ddrconfig.m_tRCD);
    config.registerProperty(*this, "RP", (uint32_t)m_ddrconfig.m_tRP);
    config.registerProperty(*this, "RAS", (uint32_t)m_ddrconfig.m_tRAS);
    config.registerProperty(*this, "CWL", (uint32_t)m_ddrconfig.m_tCWL);
    config.registerProperty(*this, "CCD", (uint32_t)m_ddrconfig.m_tCCD);
    config.registerProperty(*this, "WR", (uint32_t)m_ddrconfig.m_tWR);
    config.registerProperty(*this, "chips/rank", (uint32_t)m_ddrconfig.m_nDevicesPerRank);
    config.registerProperty(*this, "ranks", (uint32_t)(1UL<<m_ddrconfig.m_nRankBits));
    config.registerProperty(*this, "rows", (uint32_t)(1UL<<m_ddrconfig.m_nRowBits));
    config.registerProperty(*this, "columns", (uint32_t)(1UL<<m_ddrconfig.m_nColumnBits));
    config.registerProperty(*this, "freq", (uint32_t)clock.GetFrequency());
    
    RegisterSampleVariableInObject(m_busyCycles, SVC_CUMULATIVE);
}

void DDRChannel::SetClient(ICallback& cb, StorageTraceSet& sts, const StorageTraceSet& storages)
{
    if (m_callback != NULL)
    {
        throw InvalidArgumentException(*this, "DDR channel can be connected to at most one root directory.");
    }
    m_callback = &cb;

    sts = m_busy;
    p_Request.SetStorageTraces(opt(m_pipeline));
    p_Pipeline.SetStorageTraces(opt(storages));

    m_registry.registerBidiRelation(cb, *this, "ddr");
}

DDRChannel::~DDRChannel()
{
}

DDRChannelRegistry::DDRChannelRegistry(const std::string& name, Object& parent, Config& config, size_t defaultNumChannels)
    : Object(name, parent),
      m_channels(config.getValueOrDefault<size_t>(*this, "NumChannels", defaultNumChannels))
{
    for (size_t i = 0; i < m_channels.size(); ++i)
    {
        std::stringstream ss;
        ss << "channel" << i;
        Clock &ddrclock = GetKernel()->CreateClock(config.getValue<size_t>(*this, ss.str(), "Freq"));
        m_channels[i] = new DDRChannel(ss.str(), *this, ddrclock, config);
    }
}

DDRChannelRegistry::~DDRChannelRegistry()
{
    for (auto p : m_channels)
        delete p;
}

}


