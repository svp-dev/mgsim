#ifndef _DDR_MEMORY_SYS_H
#define _DDR_MEMORY_SYS_H

#include "predef.h"
#include "busst_slave_if.h"
#include "../../VirtualMemory.h"
#include "memschedulepipeline.h"
#include "../../config.h"

namespace MemSim
{

#define MSP_DEFAULT_SIZE    0x40
#define DDR_NA_ID           0xff
    
///////////////////////////////////////////
// support only fixed burst length, burst length are automatically chopped if longer than datapath
// some configurations have to be identical to different channels
//
// row hit r/w --   1. first time:  m_tRP/RCD + m_tRL/m_tWL
//                  2. normal:      m_tRL/m_tWL
//                  3. pipelined:   m_tBurst or []
//
// row miss r/w --  1. normal:      m_tRP + m_tRL/m_tWL
///////////////////////////////////////////

class DDRChannel : public MemoryState
{
    Simulator::VirtualMemory& m_pMemoryDataContainer;   

    //////////////////////////////////////
    // configuration parameters
    unsigned int m_nRanks;      // number of ranks on DIMM (only one active per DIMM)
    unsigned int m_nBanks;      // number of banks
    unsigned int m_nRows;       // number of rows
    unsigned int m_nColumns;    // number of columns

    unsigned int m_nRankBitStart;           // start position of the rank bits
    unsigned int m_nBankBitStart;           // start position of the bank bits
    unsigned int m_nRowBitStart;            // start position of the row bits
    unsigned int m_nColumnBitStart;         // start position of the column bits

    //////////////////////////////////////
    // mapping
    // dpb = lg(m_nDataPathBits) - 3                            // datapath address bit width
    // [0, dbp-1]                                               // datapath bits
    //
    // [m_nColumnBitStart, m_nColumnBitStart+m_nColumnBits-1]     // column bits
    //                                                            // assert(m_nColumnBitStart == dbp)
    //
    // [m_nBankBitStart, m_nBankBitStart+m_nBankBits-1]           // bank bits
    //                                                            // assert(m_nBankBitStart == dbp+m_nColumnBits)
    //
    // [m_nRowBitStart, m_nRowBitStart+m_nRowBits-1]              // row bits
    //
    // [m_nRankBitStart, m_nRankBitStart+m_nRankBits-1]           // rank bits

    // DDR runtime parameters
    unsigned int m_tRL;                 // read latency
    unsigned int m_tWL;                 // write latency
    unsigned int m_tBurst;              // burst delay
    unsigned int m_tCCD;                // /CAS to /CAS delay
    unsigned int m_tRPRE;               // minimum preamble time 
    unsigned int m_tRP;                 // /RAS precharge time (minimum precharge to active time)
    unsigned int m_nDataPathBits;       // data path bits for the channel, can be 64-bit or dual-channel 128 bits
    unsigned int m_nBurstLength;        // maybe the burstlength is too small for one cacheline

    MemorySchedulePipeline* m_pMSP; // memory schedule pipeline
    std::list<ST_request*>  m_lstReq;   // request buffer
    std::list<ST_request*>  m_lstReqOut;   // request output buffer

    uint64_t m_nLastRank;
    uint64_t m_nLastBank;
    uint64_t m_nLastRow;
    bool     m_bLastWrite;

    bool ScheduleRequest(ST_request* req);
    void ProcessRequest(ST_request*);
    void FunRead(ST_request*);
    void FunWrite(ST_request*);

public:

    DDRChannel(Simulator::VirtualMemory& pMemoryDataContainer, const Config& config)
      : m_pMemoryDataContainer(pMemoryDataContainer),
        m_nRanks(config.getInteger<int>("DDR_Ranks", 0)),
        m_nBanks(config.getInteger<int>("DDR_Banks", 0)),
        m_nRows(1U << config.getInteger<int>("DDR_RowBits", 0)),
        m_nColumns(1U << config.getInteger<int>("DDR_ColumnBits", 0)),
        m_tRPRE(1),
        m_tRP(config.getInteger<int>("DDR_tRP", 0)),
        m_nLastRank(DDR_NA_ID),
        m_nLastBank(DDR_NA_ID),
        m_nLastRow (DDR_NA_ID),
        m_bLastWrite(false)
    {
        unsigned int tAL  = config.getInteger<int>("DDR_tAL",  0);
        unsigned int tCL  = config.getInteger<int>("DDR_tCL",  0);
        unsigned int tCWL = config.getInteger<int>("DDR_tCWL", 0);
        unsigned int tRCD = config.getInteger<int>("DDR_tRCD", 0);
        unsigned int tRAS = config.getInteger<int>("DDR_tRAS", 0);
        unsigned int nCellSizeBits  = lg2(config.getInteger<int>("DDR_CellSize", 0)); 
        unsigned int nDevicePerRank = config.getInteger<int>("DDR_DevicesPerRank", 0);
        unsigned int nBurstLength   = config.getInteger<int>("DDR_BurstLength", 0);
        
        m_nDataPathBits = (1 << nCellSizeBits) * nDevicePerRank;

        // address mapping
        unsigned int ndp   = lg2(m_nDataPathBits / 8);
        unsigned int nbbs  = ndp   + lg2(m_nColumns);
        unsigned int nbros = nbbs  + lg2(m_nBanks);
        unsigned int nbras = nbros + lg2(m_nRows);
                
        m_nRankBitStart   = nbras;
        m_nBankBitStart   = nbbs;
        m_nRowBitStart    = nbros;
        m_nColumnBitStart = ndp;
        m_tRL    = tAL + config.getInteger<int>("DDR_tCL",  0);
        m_tWL    = tAL + config.getInteger<int>("DDR_tCWL", 0);
        m_tBurst = nBurstLength/2;
        m_tCCD   = nBurstLength/2;

        m_pMSP = new MemorySchedulePipeline(MSP_DEFAULT_SIZE);
    }

    uint64_t AddrBankID(__address_t addr) const
    {
        return (addr >> m_nBankBitStart) % m_nBanks;
    }

    uint64_t AddrRankID(__address_t addr) const
    {
        return (addr >> m_nRankBitStart) % m_nRanks;
    }

    uint64_t AddrRowID(__address_t addr) const
    {
        return (addr >> m_nRowBitStart) % m_nRows;
    }

    uint64_t AddrColumnID(__address_t addr) const
    {
        return (addr >> m_nColumnBitStart) % m_nColumns;
    }

    void ExecuteCycle();
    
    void InsertRequest(ST_request* req)
    {
        m_lstReq.push_back(req);
    }

    ST_request* GetOutputRequest() const
    {
        return (m_lstReqOut.size() == 0) ? NULL : m_lstReqOut.front();
    }

    void PopOutputRequest()
    {
        m_lstReqOut.pop_front();
    }
};


class DDRMemorySys : public sc_module, public MemoryState, public BusST_Slave_if, virtual public SimObj
{
private:
    DDRChannel m_channel;

    void Behavior();
public:
    SC_HAS_PROCESS(DDRMemorySys);

    DDRMemorySys(sc_module_name nm, sc_clock& clock, Simulator::VirtualMemory& pMemoryDataContainer, const Config& config)
      : sc_module(nm), m_channel(pMemoryDataContainer, config)
    {
        SC_METHOD(Behavior);
        sensitive << clock.posedge_event();
        dont_initialize();
    }
};


}



#endif
