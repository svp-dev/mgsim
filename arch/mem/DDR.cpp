#include "arch/mem/DDR.h"
#include "sim/config.h"
#include "sim/log2.h"
#include "sim/sampling.h"

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

    const CycleNo now = GetKernel()->GetActiveClock()->GetCycleNo();
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
            DeadlockWrite("DDR read pipeline full");
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
    const CycleNo  now     = GetKernel()->GetActiveClock()->GetCycleNo();
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

DDRChannel::DDRConfig::DDRConfig(const std::string& name, Object& parent, Clock& clock)
    : Object(name, parent),
      m_nBurstLength(GetConf("BurstLength", size_t)),
      m_tRCD (GetConf("tRCD", unsigned)),
      m_tRP  (GetConf("tRP", unsigned)),
      m_tCL  (GetConf("tCL", unsigned)),
      // tWR is expressed in DDR specs in nanoseconds, see
      // http://www.samsung.com/global/business/semiconductor/products/dram/downloads/applicationnote/tWR.pdf
      // Frequency is in MHz.
      m_tWR  (GetConf("tWR", unsigned) / 1e3 * clock.GetFrequency()),

      m_tCCD (GetConf("tCCD", unsigned)),
      m_tCWL (GetConf("tCWL", unsigned)),
      m_tRAS (GetConf("tRAS", unsigned)),

      // Address bit mapping.
      m_nDevicesPerRank (GetConf("DevicesPerRank", size_t)),
      m_nBankBits (ilog2(GetConf("Banks", size_t))),
      m_nRankBits (ilog2(GetConf("Ranks", size_t))),
      m_nRowBits (GetConf("RowBits", size_t)),
      m_nColumnBits (GetConf("ColumnBits", size_t)),

      m_nColumnStart (0),
      m_nBankStart (m_nColumnStart + m_nColumnBits),
      m_nRankStart (m_nBankStart + m_nBankBits),
      m_nRowStart (m_nRankStart + m_nRankBits),

      m_nBurstSize (m_nDevicesPerRank * m_nBurstLength)
{
    if (m_nBurstLength != 8)
        throw SimulationException(*this, "This implementation only supports m_nBurstLength = 8");

    size_t cellsize = GetConf("CellSize", size_t);
    if (cellsize != 8)
        throw SimulationException(*this, "This implementation only supports CellSize = 8");
}

DDRChannel::DDRChannel(const std::string& name, Object& parent, Clock& clock)
    : Object(name, parent),
      m_ddrconfig("config", *this, clock),
      // Initialize each rank at 'no row selected'
      m_currentRow(1 << (m_ddrconfig.m_nRankBits + m_ddrconfig.m_nBankBits), INVALID_ROW),
      m_callback(0),
      m_request(),
      m_pipeline("b_pipeline", *this, clock, m_ddrconfig.m_tCL),
      m_busy("f_busy", *this, clock, false),
      m_next_command(0),
      m_next_precharge(0),
      m_traces(),

      InitProcess(p_Request, DoRequest),
      InitProcess(p_Pipeline, DoPipeline),

      m_busyCycles(0)
{
    m_busy.Sensitive(p_Request);
    m_pipeline.Sensitive(p_Pipeline);

    RegisterModelObject(*this, "ddr");
    RegisterModelProperty(*this, "CL", (uint32_t)m_ddrconfig.m_tCL);
    RegisterModelProperty(*this, "RCD", (uint32_t)m_ddrconfig.m_tRCD);
    RegisterModelProperty(*this, "RP", (uint32_t)m_ddrconfig.m_tRP);
    RegisterModelProperty(*this, "RAS", (uint32_t)m_ddrconfig.m_tRAS);
    RegisterModelProperty(*this, "CWL", (uint32_t)m_ddrconfig.m_tCWL);
    RegisterModelProperty(*this, "CCD", (uint32_t)m_ddrconfig.m_tCCD);
    RegisterModelProperty(*this, "WR", (uint32_t)m_ddrconfig.m_tWR);
    RegisterModelProperty(*this, "chips/rank", (uint32_t)m_ddrconfig.m_nDevicesPerRank);
    RegisterModelProperty(*this, "ranks", (uint32_t)(1UL<<m_ddrconfig.m_nRankBits));
    RegisterModelProperty(*this, "rows", (uint32_t)(1UL<<m_ddrconfig.m_nRowBits));
    RegisterModelProperty(*this, "columns", (uint32_t)(1UL<<m_ddrconfig.m_nColumnBits));
    RegisterModelProperty(*this, "freq", (uint32_t)clock.GetFrequency());

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

    RegisterModelBidiRelation(cb, *this, "ddr");
}

DDRChannel::~DDRChannel()
{
}

DDRChannelRegistry::DDRChannelRegistry(const std::string& name, Object& parent, size_t defaultNumChannels)
    : Object(name, parent),
      m_channels(GetConfOpt("NumChannels", size_t, defaultNumChannels))
{
    for (size_t i = 0; i < m_channels.size(); ++i)
    {
        auto cname = "channel" + std::to_string(i);
        Clock &ddrclock = GetKernel()->CreateClock(GetSubConf(cname, "Freq", Clock::Frequency));
        m_channels[i] = new DDRChannel(cname, *this, ddrclock);
    }
}

DDRChannelRegistry::~DDRChannelRegistry()
{
    for (auto p : m_channels)
        delete p;
}

}
