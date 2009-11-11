#ifndef _DDRXML_H
#define _DDRXML_H

#include <vector>
#include <map>
using namespace std;

typedef struct _ddr_interface_t{
    int id;
    string  name;

    int tAL;     // Additive Latency  (RL = AL + CL; WL = AL + CWL), assume CWL == CL
    int tCL;     // /CAS low to valid data out (equivalent to tCAC)
    int tCWL;    // Write delay corresponding to CL
    int tRCD;    // /RAS low to /CAS low time
    int tRP;     // /RAS precharge time (minimum precharge to active time)
    int tRAS;    // Row active time (minimum active to precharge time)

    // configuration
    int nChannel;            // number of channels
    int nModeChannel;        // Mode running on multiple channels
                                      // 0 : multiple/single individual channel
                                      // 1 : merge to wider datapath

    int nRankBits;           // lg number of ranks on DIMM (only one active per DIMM)
    int nBankBits;           // lg number of banks
    int nRowBits;            // lg number of rows
    int nColumnBits;         // lg number of columns
    int nCellSizeBits;       // lg number of cell size, normally x4, x8, x16
    int nDevicePerRank;      // devices per rank
    int nDataPathBits;       // single channel datapath bits, 64 for ddr3
                             // data_path_bit == cell_size * devices_per_rank
    int nBurstLength;
} ddr_interface;

// NULL if no interfaces are load
map<int, ddr_interface*>* loadxmlddrinterfaces(const char* fname);

map<int, vector<int> >* loadxmlddrconfigurations(map<int, ddr_interface*>* pinterfaces, const char* fname);

#endif

