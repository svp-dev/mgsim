#ifndef DDR_H
#define DDR_H
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
#include "kernel.h"
#include "Memory.h"

class Config;

namespace Simulator
{

class VirtualMemory;

/// Double-Data Rate Memory
class DDRChannel : public Object
{
public:
    class ICallback
    {
    public:
        virtual bool OnReadCompleted(MemAddr address, const MemData& data) = 0;
        virtual ~ICallback() {}
    };
    
private:
    struct Request
    {
        MemAddr      address;   ///< We want something with this address
        MemData      data;      ///< With this data or size
        unsigned int offset;    ///< Current offset that we're handling
        bool         write;     ///< A write or read
        CycleNo      done;      ///< When this request is done
    };

    struct DDRConfig {
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
    unsigned int m_nDevicesPerRank; ///< Number of devices per rank
    unsigned int m_nRankBits;       ///< Log number of ranks on DIMM (only one active per DIMM)
    unsigned int m_nRowBits;        ///< Log number of rows
    unsigned int m_nColumnBits;     ///< Log number of columns
    unsigned int m_nRankStart;      ///< Start position of the rank bits
    unsigned int m_nRowStart;       ///< Start position of the row bits
    unsigned int m_nColumnStart;    ///< Start position of the column bits
    
    unsigned int m_nBurstSize;

    DDRConfig(const Clock& clock, Config&);
    };

    // Runtime parameters
    Clock&                     m_clock;
    DDRConfig                  m_ddrconfig;      ///< DDR Configuration parameters
    std::vector<unsigned long> m_currentRow;     ///< Currently selected row, for each rank
    VirtualMemory&             m_memory;         ///< The backing store with data
    ICallback&                 m_callback;       ///< The callback to notify for completion
    Request                    m_request;        ///< The current request
    Buffer<Request>            m_pipeline;       ///< Pipelined reads
    SingleFlag                 m_busy;           ///< Trigger for process
    CycleNo                    m_next_command;   ///< Minimum time for next command
    CycleNo                    m_next_precharge; ///< Minimum time for next Row Precharge
    
    // Processes
    Process p_Request;
    Process p_Pipeline;
    
    Result DoRequest();
    Result DoPipeline();
    
public:
    bool Read(MemAddr address, MemSize size);
    bool Write(MemAddr address, const void* data, MemSize size);
    
    DDRChannel(const std::string& name, Object& parent, Clock& clock, VirtualMemory& memory, Config& config);
    ~DDRChannel();
};

}

#endif
