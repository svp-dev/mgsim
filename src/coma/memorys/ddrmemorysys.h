#ifndef _DDR_MEMORY_SYS_H
#define _DDR_MEMORY_SYS_H

#include "predef.h"
#include "busst_slave_if.h"
#include "../simlink/memorydatacontainer.h"
#include "memschedulepipeline.h"

namespace MemSim{
// memory simulator namespace

#define MSP_DEFAULT_SIZE    0x40

#define DDR_NA_ID           0xff
    
///////////////////////////////////////////
// support only fixed burst length, burst length are automatically chopped if longer than datapath
// some configurations have to be identical to different channels
//
// row hit r/w --   1. first time:  m_tRP + m_tRL/m_tWL
//                  2. normal:      m_tRL/m_tWL
//                  3. pipelined:   m_tBurst or []
//
// row miss r/w --  1. normal:      m_tRP + m_tRL/m_tWL
///////////////////////////////////////////

class DDRChannel : public MemoryState, public SimObj
{
private:
    //////////////////////////////////////
    // configuration parameters
    unsigned int m_nRankBits;           // lg number of ranks on DIMM (only one active per DIMM)
    unsigned int m_nBankBits;           // lg number of banks
    unsigned int m_nRowBits;            // lg number of rows
    unsigned int m_nColumnBits;         // lg number of columns

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

    MemorySchedulePipeline*     m_pMSP; // memory schedule pipeline

    list<ST_request*>           m_lstReq;   // request buffer

    list<ST_request*>           m_lstReqOut;   // request output buffer

    uint64_t m_nLastRank;
    uint64_t m_nLastBank;
    uint64_t m_nLastRow;
    bool m_bLastWrite;

    MemoryDataContainer *m_pMemoryDataContainer;   


    bool ScheduleRequest(ST_request* req);

    void ProcessRequest(ST_request*);

    void FunRead(ST_request*);

    void FunWrite(ST_request*);

public:
    DDRChannel(MemoryDataContainer *pMemoryDataContainer, 
	       unsigned int datapathsize, 
	       unsigned int ncolstart, 
	       unsigned int ncolbits, 
	       unsigned int nbankstart, 
	       unsigned int nbankbits, 
	       unsigned int nrowstart, 
	       unsigned int nrowbits, 
	       unsigned int nrankstart, 
	       unsigned int nrankbits, 
	       unsigned int tRL, 
	       unsigned int tWL, 
	       unsigned int tBurst, 
	       unsigned int tCCD, 
	       unsigned int tRPRE, 
	       unsigned int tRP)
      : m_nRankBits(nrankbits), 
      m_nBankBits(nbankbits), 
      m_nRowBits(nrowbits), 
      m_nColumnBits(ncolbits), 
      m_nRankBitStart(nrankstart), 
      m_nBankBitStart(nbankstart), 
      m_nRowBitStart(nrowstart), 
      m_nColumnBitStart(ncolstart), 
      m_tRL(tRL), 
      m_tWL(tWL), 
      m_tBurst(tBurst), 
      m_tCCD(tCCD), 
      m_tRPRE(tRPRE), 
      m_tRP(tRP),
      m_nDataPathBits(datapathsize),
      m_pMemoryDataContainer(pMemoryDataContainer)
    {
        m_pMSP = new MemorySchedulePipeline(MSP_DEFAULT_SIZE);

        m_nLastRank = DDR_NA_ID;
        m_nLastBank = DDR_NA_ID;
        m_nLastRow = DDR_NA_ID;
        m_bLastWrite = false;


        /*
        clog << "channel parameters: " << endl;
        clog << m_nRankBits << "\t"           // lg number of ranks on DIMM (only one active per DIMM)
         << m_nBankBits << "\t"           // lg number of banks
         << m_nRowBits << "\t"            // lg number of rows
         << m_nColumnBits << "\t"         // lg number of columns
         << m_nRankBitStart << "\t"           // start position of the rank bits
         << m_nBankBitStart << "\t"           // start position of the bank bits
         << m_nRowBitStart << "\t"            // start position of the row bits
         << m_nColumnBitStart << endl;         // start position of the column bits


        // DDR runtime parameters
        clog << m_tRL << endl;                 // read latency
        clog <<  m_tWL << endl;                 // write latency

        clog << m_tBurst << endl;              // burst delay

        clog << m_tCCD << endl;                // /CAS to /CAS delay

        clog << m_tRP << endl;                 // /RAS precharge time (minimum precharge to active time)

        clog <<  m_tRPRE  << endl;               // minimum preamble time 

        clog << m_nDataPathBits << endl;       // data path bits for the channel, can be 64-bit or dual-channel 128 bits
        */

    }

    uint64_t AddrBankID(__address_t addr)
    {
        static uint64_t mask = (1 << m_nBankBits) -1;

        return (addr >> m_nBankBitStart)&mask;
    }

    uint64_t AddrRankID(__address_t addr)
    {
        static uint64_t mask = (1 << m_nRankBits) -1;

        return (addr >> m_nRankBitStart)&mask;
    }

    uint64_t AddrRowID(__address_t addr)
    {
        static uint64_t mask = (1 << m_nRowBits) -1;

        return (addr >> m_nRowBitStart)&mask;
    }

    uint64_t AddrColumnID(__address_t addr)
    {
        static uint64_t mask = (1 << m_nColumnBits) -1;

        return (addr >> m_nColumnBitStart)&mask;
    }

    unsigned int GetDataPathBits(){return m_nDataPathBits;}

    void ExecuteCycle();
    
    void InsertRequest(ST_request* req)
    {
        m_lstReq.push_back(req);
    }

    ST_request* GetOutputRequest()
    {
        if (m_lstReqOut.size() == 0)
            return NULL;
        else 
            return m_lstReqOut.front();
    }

    void PopOutputRequest()
    {
        m_lstReqOut.pop_front();
    }

    virtual void InitializeLog(const char* logName, LOG_LOCATION ll, VERBOSE_LEVEL verbose = VERBOSE_DEFAULT)
    {
        m_pMSP->InitializeLog(logName, ll, verbose);

        SimObj::InitializeLog(logName, ll, verbose);
    }

    virtual void SetVerbose(int nlevel)
    {
        m_pMSP->SetVerbose(nlevel);
        SimObj::SetVerbose(nlevel);
    }

};


class DDRMemorySys : public sc_module, public MemoryState, public BusST_Slave_if, virtual public SimObj
{
public:
    sc_in<bool> port_clk;
    //sc_fifo<ST_request*> channel_fifo_slave;

private:
    //////////////////////////////////////
    // DDR memory architectural parameters
    // timing
    unsigned int m_tAL;     // Additive Latency  (RL = AL + CL; WL = AL + CWL), assume CWL == CL
    unsigned int m_tCL;     // /CAS low to valid data out (equivalent to tCAC)
    unsigned int m_tCWL;    // Write delay corresponding to CL
    unsigned int m_tRCD;    // /RAS low to /CAS low time
    unsigned int m_tRP;     // /RAS precharge time (minimum precharge to active time)
    unsigned int m_tRAS;    // Row active time (minimum active to precharge time)

    // configuration 
    unsigned int m_nChannel;            // number of channels 
    unsigned int m_nModeChannel;        // Mode running on multiple channels
                                        // 0 : multiple/single individual channel
                                        // 1 : merge to wider datapath
                                        //
    unsigned int m_nRankBits;           // lg number of ranks on DIMM (only one active per DIMM)
    unsigned int m_nBankBits;           // lg number of banks
    unsigned int m_nRowBits;            // lg number of rows
    unsigned int m_nColumnBits;         // lg number of columns
    unsigned int m_nCellSizeBits;       // lg number of cell size, normally x4, x8, x16
    unsigned int m_nDevicePerRank;      // devices per rank
    unsigned int m_nDataPathBits;       // single channel datapath bits, 64 for ddr3
                                        // data_path_bit == cell_size * devices_per_rank

    // burst length
    unsigned int m_nBurstLength;

    //////////////////////////////////////
    // DDR runtime parameters
//    unsigned int m_tRL;                 // read latency
//    unsigned int m_tWL;                 // write latency
//
//    unsigned int m_tBurst;              // burst delay
//
//    unsigned int m_tCCD;                // /CAS to /CAS delay
//
//    unsigned int m_tRPRE;               // minimum preamble time 
//
    DDRChannel** m_pChannels;            // DDR channels

    unsigned int m_nOpChannels;


    unsigned int m_nChannelBits;
    unsigned int m_nChannelBitStart;

    //////////////////////////////////////
    // system states
    enum STATE{
        STATE_PROCESSING,
        STATE_RETRY_BUS
    };

    STATE m_nState;

    ST_request* m_pReqCur;

    list<ST_request*>   m_lstReqOut;    // reply request list

    MemoryDataContainer *m_pMemoryDataContainer;

public:
    SC_HAS_PROCESS(DDRMemorySys);

    DDRMemorySys(sc_module_name nm, MemoryDataContainer *pMemoryDataContainer)
        : SimObj(nm), sc_module(nm), m_pMemoryDataContainer(pMemoryDataContainer)
    {
        // default values for DDR3-1600 11-11-11-28 4GB DIMM
        // 240pin Unbuffered DIMM based on 2Gb B-die
        // M378B5273BH1-CF8/H9/K0
        m_tAL = 0;
        m_tCL = 11;
        m_tCWL = 11;
        m_tRCD = 11;
        m_tRP = 11;
        m_tRAS = 28;
   
        m_nRankBits = 1;
        m_nBankBits = 3;
        m_nRowBits = 15;
        m_nColumnBits = 10;
        m_nCellSizeBits = 3;
        m_nDevicePerRank = 8;
        m_nDataPathBits = 64;

        m_nChannel = 1;
        m_nModeChannel = 0;

        m_nBurstLength = 8;     // to burst 64-byte cacheline

        InitializeConfiguration();
    }

    DDRMemorySys(sc_module_name nm, MemoryDataContainer *pMemoryDataContainer, unsigned int tAL, unsigned int tCL, unsigned int tCWL, unsigned int tRCD, unsigned int tRP, unsigned int tRAS, unsigned int nRankBits, unsigned int nBankBits, unsigned int nRowBits, unsigned int nColumnBits, unsigned int nCellSizeBits, unsigned int nChannel, unsigned int nModeChannel, unsigned int nDevicePerRank, unsigned int nBurstLength)
        : SimObj(nm), sc_module(nm), 
      m_tAL(tAL), 
      m_tCL(tCL), 
      m_tCWL(tCWL), 
      m_tRCD(tRCD), 
      m_tRP(tRP), 
      m_tRAS(tRAS), 
      m_nChannel(nChannel), 
      m_nModeChannel(nModeChannel), 
      m_nRankBits(nRankBits), 
      m_nBankBits(nBankBits), 
      m_nRowBits(nRowBits), 
      m_nColumnBits(nColumnBits),
      m_nCellSizeBits(nCellSizeBits), 
      m_nDevicePerRank(nDevicePerRank),
      m_nBurstLength(nBurstLength),
      m_pMemoryDataContainer(pMemoryDataContainer)
    {

        m_nDataPathBits = (1 << m_nCellSizeBits) * m_nDevicePerRank;

        /*
        cout << m_tAL << "\n" 
        << m_tCL << "\n"
        << m_tCWL << "\n"
        << m_tRCD << "\n"
        << m_tRP << "\n"
        << m_tRAS << "\n"
        << m_nRankBits << "\n"
        << m_nBankBits << "\n"
        << m_nRowBits << "\n"
        << m_nColumnBits << "\n"
        << m_nCellSizeBits << "\n"
        << m_nDevicePerRank << "\n"
        << m_nDataPathBits << "\n"
        << m_nChannel << "\n"
        << m_nModeChannel << "\n"
        << m_nBurstLength << endl;
        */

        InitializeConfiguration();

        m_nState = STATE_PROCESSING;
        m_pReqCur = NULL;

    }

    virtual ~DDRMemorySys()
    {
        for (unsigned int i=0;i<m_nOpChannels;i++)
            delete m_pChannels[i];

        free(m_pChannels);
    }


private:
    void InitializeConfiguration()
    {
        SC_METHOD(Behavior);
        sensitive << port_clk.pos();
        dont_initialize();

        // fixed parameters for multiple channels
        unsigned int tRL = m_tAL + m_tCL;
        unsigned int tWL = m_tAL + m_tCWL;
        unsigned int tRPRE = 1;
        unsigned int tBurst = m_nBurstLength/2;
        unsigned int tCCD = tBurst;   // estimation

        switch (m_nModeChannel)
        {
        case 0:
            {
                // address mapping
                unsigned int ndp = lg2(m_nDataPathBits/8);
                unsigned int nbcs = ndp;
                unsigned int nbbs = nbcs + m_nColumnBits;
                unsigned int nbros = nbbs + m_nBankBits;
                unsigned int nbras = nbros + m_nRowBits;
                m_nChannelBits = lg2(m_nChannel);
                m_nChannelBitStart = nbras + m_nRankBits;
                
                m_pChannels = (DDRChannel**)malloc(sizeof(DDRChannel*)*m_nChannel);
                for (unsigned int i=0;i<m_nChannel;i++)
                    m_pChannels[i] = new DDRChannel(m_pMemoryDataContainer, m_nDataPathBits, nbcs, m_nColumnBits, nbbs, m_nBankBits, nbros, m_nRowBits, nbras, m_nRankBits, tRL, tWL, tBurst, tCCD, tRPRE, m_tRP);

                m_nOpChannels = m_nChannel;
            }
            break;

        case 1:
            {
                // only one channel
                // address mapping
                unsigned int ndp = lg2(m_nDataPathBits/8);
                unsigned int nbcs = ndp;
                unsigned int nbbs = nbcs + m_nColumnBits;
                unsigned int nbros = nbbs + m_nBankBits;
                unsigned int nbras = nbros + m_nRowBits;
                m_nChannelBits = 0;
                m_nChannelBitStart = nbras + m_nRankBits;

                m_pChannels = (DDRChannel**)malloc(sizeof(DDRChannel*));
                unsigned int burstlength = ceil(((double)g_nCacheLineSize*8)/(m_nDataPathBits*m_nChannel));

                m_pChannels[0] = new DDRChannel(m_pMemoryDataContainer, m_nDataPathBits*m_nChannel, nbcs, m_nColumnBits, nbbs, m_nBankBits, nbros, m_nRowBits, nbras, m_nRankBits, tRL, tWL, burstlength/2, tCCD, tRPRE, m_tRP);
                m_nOpChannels = 1;
            }
            break;

        default:
            cerr << "unsupported mode" << endl;
            break;
        }

    }

    void Behavior();

    unsigned int ChannelSelect(ST_request* req)
    {
        static uint64_t mask = (1 << m_nChannelBits) -1;

        __address_t addr = req->getreqaddress();

        if (m_nChannelBits == 0)
            return 0;
        else
            return (unsigned int)((addr >> m_nChannelBitStart)&mask);
    }

    void InsertRequest(ST_request* req)
    {
        m_pChannels[ChannelSelect(req)]->InsertRequest(req);
    }

    void SendRequests();

public:
    // functions not needed, should be removed totally from the framework later
    __address_t StartAddress() const {return 0;}
    __address_t EndAddress() const {return 0;}

    // simcontrol
    virtual void InitializeLog(const char* logName, LOG_LOCATION ll, VERBOSE_LEVEL verbose = VERBOSE_DEFAULT)
    {
        for (unsigned int i=0;i<m_nOpChannels;i++)
            m_pChannels[i]->InitializeLog(logName, ll, verbose);

        SimObj::InitializeLog(logName, ll, verbose);
    }

    virtual void SetVerbose(int nlevel)
    {
        for (unsigned int i=0;i<m_nOpChannels;i++)
            m_pChannels[i]->SetVerbose(nlevel);
        SimObj::SetVerbose(nlevel);
    }

};


}



#endif
