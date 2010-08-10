DDR simulation notes
====================


Notes from ZL
-------------

``ddrmemorysys.h``::

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

   [...]

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

``ddrmemorysys.cpp``::

    bool DDRChannel::ScheduleRequest(ST_request* req)
    {
        assert(req != NULL);
        __address_t addr = req->getlineaddress();
        bool bwrite=false;
    
        if (req->type == MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA)
            bwrite = true;
        else if (req->type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA)
            bwrite = false;
        else assert(false);
    
        if ((AddrRowID(addr) != m_nLastRow) || (AddrRankID(addr) != m_nLastRank))
        {
            if (m_pMSP->ScheduleNext(req, m_tRP+m_tBurst, bwrite?m_tWL:m_tRL))
            {
                m_nLastRank = AddrRankID(addr);
                m_nLastBank = AddrBankID(addr);
                m_nLastRow = AddrRowID(addr);
                m_bLastWrite = bwrite;
    
                return true;
            }
            else
                return false;
        }
        else
        {
            if (m_pMSP->ScheduleNext(req, m_tBurst, bwrite?m_tWL:m_tRL))
            {
                m_nLastRank = AddrRankID(addr);
                m_nLastBank = AddrBankID(addr);
                m_nLastRow = AddrRowID(addr);
                m_bLastWrite = bwrite;
    
                return true;
            }
            else
                return false;
        }
    }

Notes from ML
-------------

``DDR.h``::

    /*
    === Overview of DDR (Double Data Rate) SDRAM ===
    
    DDR uses modules ("DIMMs") that each has a number of DRAM devices ("chips")
    on them. The number of devices on a module is a multiple of 8 (Non-ECC) or
    9 (ECC). The devices on a module can be grouped into 1, 2 or 4 ranks.
    Only 1 rank on a module can be active. Each device has a 4 or 8 bit data path.
    Thus, for the full 64-bit data path, 16 or 8 devices are accessed in parallel.
    A memory device is essentially several parallel-accessed banks, each a 2D
    grid of row and columns.
    
    DDR has a prefetch buffer that buffers N consecutive words in a single read
    and streams them out on the I/O bus, running at a higher frequency (a prefetch
    buffer of 8 words requires a frequency multiplier on a double-date rate I/O
    bus of 4).
    
    For instance, the Micron MT18JSF25672PD is a 2 GB DDR3 DIMM with a 64-bit
    data bus and ECC. It uses 18 (sixteen + two for ECC) Micron MT41J128M8 memory
    devices, which have a 128 MB capacity and a 8-bit data bus, arranged in 2
    ranks.
    The 128 MB memory device internally has eight banks, each with 16384 rows of
    128 columns of 64 bits. Per memory clock cycle, 64 bits can be read with
    pipelined column reads. These 64 bits are latched and returned over an 8-bit
    databus in 4 double pumped I/O bus cycles (which has a 4x multiplier w.r.t.
    the memory clock).
    
    Accessing a memory device involves the following:
    * If the wrong row is opened, it must be precharged (closed) with a Row
      Precharge (RP). This has a latency of tRP cycles.
    * Opening the desired row. It takes tRCD (RAS to CAS Delay cycles before the
      columns in the row can be read. Multiple columns can be read or written
      without reopening the row.
    * Selecting the column from an open row. The data is available on the output
      pins after a tCL (CAS Latency) latency.
    
    As such, the latency for a read is:
    * tCL + tRCD cycles if no row is open.
    * tCL + tRCD + tRP cycles if the wrong row is open
    * tCL cycles if the correct row is open.
    
    Note that a row cannot be precharged (closed) until at least tWR (Write
    Recovery) cycles after the last write and at least tRAS (Row Active to
    Precharge Delay) cycles after opening the row.
    
    Each generation of DDR increases the maximum size of the memory device and
    doubled the I/O bus frequency multiplier (and, consequently, the prefetch
    buffer size). Below is an overview of some common DDR modules:
    
      Name    Mem Clock I/O Clock Bandwidth Module Name
    --------- --------- --------- --------- -----------
    DDR1-200   100 Mhz   100 MHz   1.6 GB/s   PC-1600
    DDR1-266   133 MHz   133 MHz   2.1 GB/s   PC-2100
    DDR1-333   166 MHz   166 MHz   2.7 GB/s   PC-2700
    DDR1-400   200 MHz   200 MHz   3.2 GB/s   PC-3200
    DDR2-400   100 Mhz   200 MHz   3.2 GB/s  PC2-3200
    DDR2-533   133 MHz   266 MHz   4.2 GB/s  PC2-4200
    DDR2-667   166 MHz   333 MHz   5.3 GB/s  PC2-5300
    DDR2-800   200 MHz   400 MHz   6.4 GB/s  PC2-6400
    DDR2-1066  266 MHz   533 MHz   8.5 GB/s  PC2-8500
    DDR3-800   100 MHz   400 MHz   6.4 GB/s  PC3-6400
    DDR3-1066  133 MHz   533 MHz   8.5 GB/s  PC3-8500
    DDR3-1333  166 MHz   667 MHz  10.6 GB/s PC3-10600
    DDR3-1600  200 MHz   800 MHz  12.8 GB/s PC3-12800
    
    Bandwidth of DDR memory is identified by the number behind it. For instance,
    DDR3-800 means the data rate is 800 MHz (thus, the I/O bus frequency is 400
    MHz and the memory clock 100 MHz). Together with a 64-bit wide databus and
    2 transfers/cycle, DDR3-800 can support up to 6.4 GB/s.
    */   

    [...]

    unsigned int m_tBusMult;        ///< I/O Bus clock multiplier
    unsigned int m_nBurstLength;    ///< Size of a single burst

    // Timing configuration
    unsigned int m_tRCD;    ///< RAS to CAS Delay (row open)
    unsigned int m_tRP;     ///< Row Precharge Delay (row close)
    unsigned int m_tCL;     ///< CAS latency (delay of column read)
    unsigned int m_tWR;     ///< Write Recovery delay (min time after write before row close)
    unsigned int m_tCCD;    ///< CAS to CAS Delay (time between read commands)
    unsigned int m_tCWL;    ///< CAS Write Latency (time for a write command)
    unsigned int m_tRAS;    ///< Row Active Time (min time after row open before row close)

    // Address configuration
    unsigned int m_nDevicePerRank;  ///< Number of devices per rank
    unsigned int m_nRankBits;       ///< Log number of ranks on DIMM (only one active per DIMM)
    unsigned int m_nRowBits;        ///< Log number of rows
    unsigned int m_nColumnBits;     ///< Log number of columns
    unsigned int m_nRankStart;      ///< Start position of the rank bits
    unsigned int m_nRowStart;       ///< Start position of the row bits
    unsigned int m_nColumnStart;    ///< Start position of the column bits


``DDR.cpp``::

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

    [...]

    // Constructor:

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
