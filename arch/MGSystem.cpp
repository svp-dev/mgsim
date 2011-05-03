#include "MGSystem.h"

#include "mem/SerialMemory.h"
#include "mem/ParallelMemory.h"
#include "mem/BankedMemory.h"
#include "mem/RandomBankedMemory.h"
#include "mem/coma/COMA.h"
#include "mem/zlcoma/COMA.h"

#include "arch/dev/NullIO.h"
#include "arch/dev/lcd.h"
#include "arch/dev/RTC.h"
#include "arch/dev/Display.h"

#include "loader.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <cmath>
#include <limits>
#include <cxxabi.h>
#include <fnmatch.h>
#include <cstring>

using namespace Simulator;
using namespace std;

// WriteConfiguration()

// This function exposes the Microgrid configuration to simulated
// programs through the memory interface. The configuration data
// starts at the address given by local register 2 in the first
// thread created on the system.

// The configuration is composed of data blocks, each block
// composed of 32-bit words. The first word in each block is a
// "block type/size" tag indicating the type and size of the
// current block.

// A tag set to 0 indicates the end of the configuration data.
// A non-zero tag is a word with bits 31-16 set to the block type
// and bits 15-0 are set to the block size (number of words to
// next block).
// From each tag, the address of the tag for the next block is (in
// words) A_next = A_cur + size.

// Current block types:

// - architecture type v1, organized as follows:
//     - word 0 after tag: architecture model (1 for simulated microgrid, others TBD)
//     - word 1: ISA type (1 = alpha, 2 = sparc)
//     - word 2: number of FPUs (0 = no FPU)
//     - word 3: memory type (0 = unknown, for other values see enum MEMTYPE in MGSystem.h)

// - timing words v1, organized as follows:
//     - word 0 after tag: core frequency (in MHz)
//     - word 1 after tag: external memory bandwidth (in 10^6bytes/s)

// - timing words v2, organized as follows:
//     - word 0 after tag: master frequency (in MHz)
//     - word 1 after tag: core frequency (in MHz)
//     - word 2 after tag: memory frequency (in MHz)
//     - word 3 after tag: DDR frequency (in MHz)

// - timing words v3, organized as follows:
//     - word 0 after tag: master frequency (in MHz)
//     - word 1 after tag: core frequency (in MHz)
//     - word 2 after tag: memory frequency (in MHz)
//     - word 3 after tag: DDR frequency (in MHz)
//     - word 4 after tag: number of DDR channels

// - cache parameters words v1, organized as follows:
//     - word 0 after tag: cache line size (in bytes)
//     - word 1: L1 I-cache size (in bytes)
//     - word 2: L1 D-cache size (in bytes)
//     - word 3: number of L2 caches
//     - word 4: L2 cache size per cache (in bytes)

// - concurrency resource words v1, organized as follows:
//     - word 0 after tag: number of family entries per core
//     - word 1: number of thread entries per core
//     - word 2: number of registers in int register file
//     - word 3: number of registers in float register file

// - place layout v1, organized as follows:
//     - word 0 after tag: number of cores (P)
//     - word 1 ... P: (ringID << 16) | (coreID << 0)
//     (future layout configuration formats may include topology information)

// - place layout v2, organized as follows:
//     - word 0 after tag: number of cores (P)
//     - word 1 ... P: (y << 24) | (x << 16) | (coreType)
//     coreType:
//      0: Normal core
//      1: I/O core

// - Core information v1 - shared by all cores
//     - word 0 after tag: number of configurable ancillary core registers
//     - word 1: number of performance counters supported
//     - word 2: low bits of base address of perf counters
//     - word 3: high bits bits of base address of perf counters
//     - word 4: low bits base address of debug stdout channel (0 if not supported)
//     - word 5: high bits base address of debug stdout channel (0 if not supported)
//     - word 6: low bits base address of debug stderr channel (0 if not supported)
//     - word 7: high bits base address of debug stderr channel (0 if not supported)

// - I/O core information v1
//     - word 0: core ID on grid
//     - word 1: (number of interrupts channels supported << 16) | (number of devices supported)
//     - word 2: (bus ID << 16) | (device ID of core on bus)
//     - word 3: address space bits per device
//     - word 4: low bits of PIC address space
//     - word 5: high bits of PIC address space
//     - word 6: low bits of async I/O address space
//     - word 7: high bits of async I/O address space

// - I/O bus information v1
//     - word 0: bus ID
//     - word 1: bus type (0 for unknown)
//     - word 2: I/O bus frequency

// - I/O device information v1
//     - word 0: (bus ID << 16) | (device ID on bus)
//     - word 1: (vendor << 16) | (model)
//     - word 2: revision

#define CONFTAG_ARCH_V1       1
#define CONFTAG_TIMINGS_V1    2
#define CONFTAG_CACHE_V1      3
#define CONFTAG_CONC_V1       4
#define CONFTAG_LAYOUT_V1     5
#define CONFTAG_TIMINGS_V2    6
#define CONFTAG_TIMINGS_V3    7
#define CONFTAG_LAYOUT_V2     8
#define CONFTAG_COREINFO_V1   9
#define CONFTAG_IOCOREINFO_V1 10
#define CONFTAG_IODEVINFO_V1  11
#define MAKE_TAG(Type, Size) (uint32_t)(((Type) << 16) | ((Size) & 0xffff))

static vector<string> Tokenize(const string& str, const string& sep)
{
    vector<string> tokens;
    for (size_t next, pos = str.find_first_not_of(sep); pos != string::npos; pos = next)
    {
        next = str.find_first_of(sep, pos);
        if (next == string::npos)
        {
            tokens.push_back(str.substr(pos));
        }
        else
        {
            tokens.push_back(str.substr(pos, next - pos));
            next = str.find_first_not_of(sep, next);
        }
    }
    return tokens;
}

void MGSystem::FillConfWords(ConfWords& words) const
{
    uint32_t cl_sz = m_config.getValue<uint32_t>("CacheLineSize");

    // configuration words for architecture type v1

    words << MAKE_TAG(CONFTAG_ARCH_V1, 4)
          << 1 // simulated microgrid
#if defined(TARGET_MTALPHA)
          << 1 // alpha
#else
          << 2 // sparc
#endif
          << m_fpus.size()
          << m_memorytype


    // timing words v3
          << MAKE_TAG(CONFTAG_TIMINGS_V3, 5)
          << m_kernel.GetMasterFrequency()
          << m_config.getValue<uint32_t>("CoreFreq")
          << m_config.getValue<uint32_t>("MemoryFreq")
          << ((m_memorytype == MEMTYPE_COMA_ZL || m_memorytype == MEMTYPE_COMA_ML) ?
              m_config.getValue<uint32_t>("*.DDR.Channel*.Freq")
              : 0 /* no timing information if memory system is not COMA */)
          << ((m_memorytype == MEMTYPE_COMA_ZL || m_memorytype == MEMTYPE_COMA_ML) ?
              m_config.getValue<uint32_t>("*.Memory.NumRootDirectories")
              : 0 /* no DDR channels */)

    // cache parameter words v1

          << MAKE_TAG(CONFTAG_CACHE_V1, 5)
          << cl_sz
          << (cl_sz
              * m_config.getValue<uint32_t>("*.CPU*.ICache.Associativity")
              * m_config.getValue<uint32_t>("*.CPU*.ICache.NumSets"))
          << (cl_sz
              * m_config.getValue<uint32_t>("*.CPU*.DCache.Associativity")
              * m_config.getValue<uint32_t>("*.CPU*.DCache.NumSets"));
    if (m_memorytype == MEMTYPE_COMA_ZL || m_memorytype == MEMTYPE_COMA_ML)
        words << (m_procs.size()
                  / m_config.getValue<uint32_t>("NumProcessorsPerL2Cache")) // FIXME: COMA?
              << (cl_sz
                  * m_config.getValue<uint32_t>("*.Memory.L2CacheAssociativity")
                  * m_config.getValue<uint32_t>("*.Memory.L2CacheNumSets"));
    else
        words << 0 << 0;

    // concurrency resources v1

    words << MAKE_TAG(CONFTAG_CONC_V1, 4)
          << m_config.getValue<uint32_t>("*.CPU*.Families.NumEntries")
          << m_config.getValue<uint32_t>("*.CPU*.Threads.NumEntries")
          << m_config.getValue<uint32_t>("*.CPU*.Registers.NumIntRegisters")
          << m_config.getValue<uint32_t>("*.CPU*.Registers.NumFltRegisters")

        ;

    // place layout v2
    PSize numProcessors = m_config.getValue<PSize>("NumProcessors");

    words << MAKE_TAG(CONFTAG_LAYOUT_V2, numProcessors + 1)
          << numProcessors;

    // Store the core information
    for (size_t i = 0; i < numProcessors; ++i)
    {
        unsigned int y = 0;
        unsigned int x = i;
        unsigned int type = 0;

        if (m_procbusmapping.find(i) != m_procbusmapping.end())
        {
            // The processor is connected to an I/O bus
            type = 1;
        }

        words << ((y << 24) | (x << 16) | type);
    }

    // core information
    MemAddr pcbase = m_config.getValue<MemAddr>("*.CPU*.PerfCounters.MMIO_BaseAddr"),
        stderrbase = m_config.getValue<MemAddr>("*.CPU*.Debug_Stderr.MMIO_BaseAddr"),
        stdoutbase = m_config.getValue<MemAddr>("*.CPU*.Debug_Stdout.MMIO_BaseAddr");

    // I/O core information
    words << MAKE_TAG(CONFTAG_COREINFO_V1, 8)
          << 0 // FIXME: number of ancillary core registers
          << Processor::PerfCounters::numCounters
          << (pcbase & 0xffffffff)
          << (pcbase >> 32)
          << (stdoutbase & 0xffffffff)
          << (stdoutbase >> 32)
          << (stderrbase & 0xffffffff)
          << (stderrbase >> 32);
        
    // after last block
    words << 0 << 0;

}

MemAddr MGSystem::WriteConfiguration()
{

    ConfWords words;
    FillConfWords(words);

    MemAddr base;
    if (!m_memory->Allocate(words.data.size() * sizeof words.data[0], IMemory::PERM_READ, base))
    {
        throw runtime_error("Unable to allocate memory to store configuration data");
    }
    m_memory->Write(base, &words.data[0], words.data.size() * sizeof words.data[0]);
    return base;
}

uint64_t MGSystem::GetOp() const
{
    uint64_t op = 0;
    for (size_t i = 0; i < m_procs.size(); ++i) {
        op += m_procs[i]->GetPipeline().GetOp();
    }
    return op;
}

uint64_t MGSystem::GetFlop() const
{
    uint64_t flop = 0;
    for (size_t i = 0; i < m_procs.size(); ++i) {
        flop += m_procs[i]->GetPipeline().GetFlop();
    }
    return flop;
}

#define MAXCOUNTS 30

struct my_iomanip_i { };
template<typename _CharT, typename _Traits>
std::basic_ostream<_CharT, _Traits>& operator<<(std::basic_ostream<_CharT, _Traits>&os, my_iomanip_i _unused)
{
    os << std::left << std::fixed << std::setprecision(2) << std::setw(9);
    return os;
}

struct my_iomanip_f { };
template<typename _CharT, typename _Traits>
std::basic_ostream<_CharT, _Traits>& operator<<(std::basic_ostream<_CharT, _Traits>&os, my_iomanip_f _unused)
{
    os << std::left << std::scientific << std::setprecision(6) << std::setw(12);
    return os;
}

struct my_iomanip_p { };
template<typename _CharT, typename _Traits>
std::basic_ostream<_CharT, _Traits>& operator<<(std::basic_ostream<_CharT, _Traits>&os, my_iomanip_p _unused)
{
    os << std::left << std::setprecision(1) << std::setw(8);
    return os;
}

static string GetClassName(const type_info& info)
{
    const char* name = info.name();

    // __cxa_demangle requires an output buffer
    // allocated with malloc(). Provide it.
    size_t len = 1024;
    char *buf = (char*)malloc(len);
    assert(buf != 0);

    int status;

    char *res = abi::__cxa_demangle(name, buf, &len, &status);

    if (res && status == 0)
    {
        string ret = res;
        free(res);
        return ret;
    }
    else
    {
        if (res) free(res);
        else free(buf);
        return name;
    }
}

static
void GetComponents(map<string, Object*>& ret, Object *cur, const string& pat)
{
    for (unsigned int i = 0; i < cur->GetNumChildren(); ++i)
    {
        Object* child = cur->GetChild(i);
        string childname = child->GetFQN();
        if (FNM_NOMATCH != fnmatch(pat.c_str(), childname.c_str(), 0))
        {
            ret[childname] = child;
        }
        GetComponents(ret, child, pat);
    }
}

map<string, Object*> MGSystem::GetComponents(const string& pat)
{
    map<string, Object*> ret;
    ::GetComponents(ret, &m_root, pat);

    if (ret.empty())
    {
        // try to match with the system name inserted as prefix.
        string syspat = m_root.GetName() + '.' + pat;
        ::GetComponents(ret, &m_root, syspat);
    }
    
    return ret;
}

// Print all components that are a child of root
static void PrintComponents(ostream& out, const Object* cur, const string& indent, const string& pat, size_t levels, size_t cur_level, bool cur_printing)
{
    for (unsigned int i = 0; i < cur->GetNumChildren(); ++i)
    {
        const Object* child = cur->GetChild(i);
        string childname = child->GetName();
        string newindent = indent;
        bool new_printing = cur_printing;

        if (!new_printing)
        {
            if (FNM_NOMATCH != fnmatch(pat.c_str(), childname.c_str(), 0))
            {
                new_printing = true;
            }
        }

        if (new_printing && (levels == 0 || cur_level < levels))
        {
            string str = indent + child->GetName();
            
            out << setfill(' ') << setw(30) << left << str << right << " ";

            const Inspect::ListCommands *lc = dynamic_cast<const Inspect::ListCommands*>(child);
            if (lc != NULL)
            {
                lc->ListSupportedCommands(out);
            }
            else
            {
                out << "        ";
            }

            out << " "
                << GetClassName(typeid(*child)) << endl;

            newindent += "  ";
        }

        PrintComponents(out, child, newindent, pat, levels, cur_level+1, new_printing);
    }
}

void MGSystem::PrintComponents(ostream& out, const string& pat, size_t levels) const
{
    ::PrintComponents(out, &m_root, "", pat, levels, 0, false);
}

void MGSystem::PrintCoreStats(ostream& os) const {
    struct my_iomanip_i fi;
    struct my_iomanip_f ff;
    struct my_iomanip_p fp;
    const char sep = ' ';

    const size_t P = m_procs.size();
    enum ct { I, F, PC };
    struct dt { uint64_t i; float f; };
    struct dt c[P][MAXCOUNTS];
    enum ct types[MAXCOUNTS];

    size_t i, j;
    
    memset(c, 0, sizeof(struct dt)*P*MAXCOUNTS);

    // Collect the data
    for (i = 0; i < P; ++i) {
        Processor &p = *m_procs[i];
        const Processor::Pipeline& pl = p.GetPipeline();

        j = 0;
        types[j] = I; c[i][j++].i = pl.GetOp();
        types[j] = I; c[i][j++].i = pl.GetFlop();

        types[j] = types[j+1] = types[j+2] = types[j+3] = I;
        c[i][j].i = c[i][j+1].i = c[i][j+2].i = c[i][j+3].i = 0;
        pl.CollectMemOpStatistics(c[i][j].i, c[i][j+1].i, c[i][j+2].i, c[i][j+3].i);
        j += 4;

        types[j] = PC; c[i][j++].f = 100. * p.GetRegFileAsyncPortActivity();
        types[j] = I; c[i][j++].i = pl.GetTotalBusyTime();
        types[j] = I; c[i][j++].i = pl.GetNStages();
        types[j] = I; c[i][j++].i = pl.GetStagesRun();
        types[j] = PC; c[i][j++].f = 100. * pl.GetEfficiency();
        types[j] = PC; c[i][j++].f = 100. * (float)pl.GetOp() / (float)pl.GetCycleNo();
        types[j] = I; c[i][j++].i = p.GetMaxThreadsAllocated();
        types[j] = I; c[i][j++].i = p.GetTotalThreadsAllocated();
        types[j] = I; c[i][j++].i = p.GetThreadTableSize();
        types[j] = PC; c[i][j++].f = 100. * p.GetThreadTableOccupancy();
        types[j] = I; c[i][j++].i = p.GetMaxFamiliesAllocated();
        types[j] = I; c[i][j++].i = p.GetTotalFamiliesAllocated();
        types[j] = I; c[i][j++].i = p.GetFamilyTableSize();
        types[j] = PC; c[i][j++].f = 100. * p.GetFamilyTableOccupancy();
        types[j] = I; c[i][j++].i = p.GetMaxAllocateExQueueSize();
        types[j] = I; c[i][j++].i = p.GetTotalAllocateExQueueSize();
        types[j] = F; c[i][j++].f = p.GetAverageAllocateExQueueSize();

    }

    const size_t NC = j;

    // compute min, max, total, average
    struct dt dmin[NC];
    struct dt dmax[NC];
    struct dt dtotal[NC];
    float davg[NC][2];
    size_t activecores;

    for (j = 0; j < NC; ++j)
    {
        dmin[j].i = numeric_limits<uint64_t>::max();
        dmin[j].f = numeric_limits<float>::max();
        dmax[j].i = 0; dmax[j].f = numeric_limits<float>::min();
        dtotal[j].i = 0; dtotal[j].f = 0.;
    }

    for (i = 0, activecores = 0; i < P; ++i) {
        if (c[i][0].i == 0) // core inactive; do not count
            continue;

        ++ activecores;
        for (j = 0; j < NC; ++j) {
            dmin[j].i = min(dmin[j].i, c[i][j].i);
            dmin[j].f = min(dmin[j].f, c[i][j].f);
            dmax[j].i = max(dmax[j].i, c[i][j].i);
            dmax[j].f = max(dmax[j].f, c[i][j].f);
            dtotal[j].i += c[i][j].i;
            dtotal[j].f += c[i][j].f;
        }
    }

    for (j = 0; j < NC; ++j) {
        davg[j][0] = (float)dtotal[j].i / (float)activecores;
        davg[j][1] = dtotal[j].f / (float)activecores;
    }

    // print the data

    os << "## core statistics:" << endl
       << "# P " << sep
       << fi << "iops" << sep
       << fi << "flops" << sep
       << fi << "lds" << sep
       << fi << "sts" << sep
       << fi << "ibytes" << sep
       << fi << "obytes" << sep
       << fp << "regf_act" << sep
       << fi << "plbusy" << sep
       << fi << "plstgs" << sep
       << fi << "plstgrun" << sep
       << fp << "pl%busy" << sep
       << fp << "pl%eff" << sep
       << fi << "ttmax" << sep
       << fi << "ttotal" << sep
       << fi << "ttsize" << sep
       << fp << "tt%occ" << sep
       << fi << "ftmax" << sep
       << fi << "ftotal" << sep
       << fi << "ftsize" << sep
       << fp << "ft%occ" << sep
       << fi << "xqmax" << sep
       << fi << "xqtot" << sep
       << ff << "xqavg" << sep
       << endl;

    os << "# per-core values" << endl;
    for (i = 0; i < P; ++i) {
        if (c[i][0].i == 0)  continue; // unused core
        os << setw(4) << i << sep;
        for (j = 0; j < NC; ++j)
            if (types[j] == I)
                os << fi << c[i][j].i << sep;
            else if (types[j] == PC)
                os << fp << c[i][j].f << sep;
            else
                os << ff << c[i][j].f << sep;
        os << endl;
    }

/*
    os << "# minimas - all active cores" << endl
       << setw(4) << activecores << sep;
    for (j = 0; j < NC; ++j)
        if (types[j] == I)
            os << fi << dmin[j].i << sep;
        else
            os << ff << dmin[j].f << sep;
    os << endl
       << "# maxima - all active cores" << endl
       << setw(4) << activecores << sep;
    for (j = 0; j < NC; ++j)
        if (types[j] == I)
            os << fi << dmin[j].i << sep;
        else
            os << ff << dmin[j].f << sep;
    os << endl;
*/
    os << "# cumulative - all active cores" << endl
       << setw(4) << activecores << sep;
    for (j = 0; j < NC; ++j)
        if (types[j] == I)
            os << fi << dtotal[j].i << sep;
        else if (types[j] == PC)
            os << fp << dtotal[j].f << sep;
        else
            os << ff << dtotal[j].f << sep;
    os << endl
       << "# average per core = cumulative/" << activecores << endl
       << setw(4) << activecores << sep;
    for (j = 0; j < NC; ++j)
        if (types[j] == I)
            os << fi << davg[j][0] << sep;
        else if (types[j] == PC)
            os << fp << davg[j][1] << sep;
        else
            os << ff << davg[j][1] << sep;

    os << endl;

    os << "## descriptions:" << endl
       << "# P: core ID / number of active cores" << endl
       << "# iops: number of instructions executed" << endl
       << "# flops: number of floating-point instructions issued" << endl
       << "# lds: number of load instructions executed" << endl
       << "# sts: number of store instructions executed" << endl
       << "# ibytes: number of bytes loaded from L1 cache into core" << endl
       << "# obytes: number of bytes stored into L1 cache from core" << endl
       << "# regf_act: register file async port activity (= 100. * ncycles_asyncport_busy / ncorecycles_total)" << endl
       << "# plbusy: number of corecycles the pipeline was active" << endl
       << "# plstgs: number of pipeline stages" << endl
       << "# plstgrun: cumulative number of corecycles active in all pipeline stages" << endl
       << "# pl%busy: pipeline efficiency while active (= 100. * plstgrun / plstgs / plbusy)" << endl
       << "# pl%eff: pipeline efficiency total (= 100. * iops / ncorecycles_total)" << endl
       << "# ttmax: maximum of thread entries simulatenously allocated" << endl
       << "# ttotal: cumulative number of thread entries busy (over mastertime)" << endl
       << "# ttsize: thread table size" << endl
       << "# tt%occ: thread table occupancy (= 100. * ttotal / ttsize / nmastercycles_total)" << endl
       << "# ttmax: maximum of family entries simulatenously allocated" << endl
       << "# ftotal: cumulative number of family entries busy (over mastertime)" << endl
       << "# ftsize: family table size" << endl
       << "# ft%occ: family table occupancy (= 100. * ftotal / ftsize / nmastercycles_total)" << endl
       << "# xqmax: high water mark of the exclusive allocate queue size" << endl
       << "# xqtot: cumulative exclusive allocate queue size (over mastertime)" << endl
       << "# xqavg: average size of the exclusive allocate queue (= xqtot / nmastercycles_total)" << endl;

}

void MGSystem::PrintMemoryStatistics(ostream& os) const {
    uint64_t nr = 0, nrb = 0, nw = 0, nwb = 0, nrext = 0, nwext = 0;

    m_memory->GetMemoryStatistics(nr, nw, nrb, nwb, nrext, nwext);
    os << nr << "\t# number of load reqs. by the L1 cache from L2" << endl
       << nrb << "\t# number of bytes loaded by the L1 cache from L2" << endl
       << nw << "\t# number of store reqs. by the L1 cache to L2" << endl
       << nwb << "\t# number of bytes stored by the L1 cache to L2" << endl
       << nrext << "\t# number of cache lines read from the ext. mem. interface" << endl
       << nwext << "\t# number of cache lines written to the ext. mem. interface" << endl;

}

void MGSystem::PrintState(const vector<string>& arguments) const
{
    typedef map<string, RunState> StateMap;

    // This should be all non-idle processes
    for (const Clock* clock = m_kernel.GetActiveClocks(); clock != NULL; clock = clock->GetNext())
    {
        StateMap   states;
        streamsize length = 0;

        if (clock->GetActiveProcesses() != NULL || clock->GetActiveStorages() != NULL || clock->GetActiveArbitrators() != NULL)
        {
            cout << clock->GetFrequency() << " MHz clock (next tick at cycle " << dec << clock->GetNextTick() << "):" << endl;

            for (const Process* process = clock->GetActiveProcesses(); process != NULL; process = process->GetNext())
            {
                const string name = process->GetName();
                states[name] = process->GetState();
                length = max(length, (streamsize)name.length());
            }

            cout << left << setfill(' ');
            for (StateMap::const_iterator p = states.begin(); p != states.end(); ++p)
            {
                cout << "- " << setw(length) << p->first << ": ";
                switch (p->second)
                {
                case STATE_IDLE:     assert(0); break;
                case STATE_ACTIVE:   cout << "active"; break;
                case STATE_DEADLOCK: cout << "stalled"; break;
                case STATE_RUNNING:  cout << "running"; break;
                case STATE_ABORTED:  assert(0); break;
                }
                cout << endl;
            }

            if (clock->GetActiveStorages() != NULL)
            {
                cout << "- One or more storages need updating" << endl;
            }

            if (clock->GetActiveArbitrators() != NULL)
            {
                cout << "- One or more arbitrators need updating" << endl;
            }

            cout << endl;
        }
    }

    int width = (int)log10(m_procs.size()) + 1;
    for (size_t i = 0; i < m_procs.size(); ++i)
    {
        if (!m_procs[i]->IsIdle()) {
            cout << "Processor " << dec << right << setw(width) << i << ": non-empty" << endl;
        }
    }
}

void MGSystem::PrintAllStatistics(ostream& os) const
{
    os << dec;
    os << GetKernel().GetCycleNo() << "\t# mastercycle counter" << endl
       << GetOp() << "\t# total executed instructions" << endl
       << GetFlop() << "\t# total issued fp instructions" << endl;
    PrintCoreStats(os);
    os << "## memory statistics:" << endl;
    PrintMemoryStatistics(os);
}

// Steps the entire system this many cycles
void MGSystem::Step(CycleNo nCycles)
{
    m_breakpoints.Resume();
    RunState state = GetKernel().Step(nCycles);
    if (state == STATE_IDLE)
    {
        // An idle state might actually be deadlock if there's a suspended thread.
        // So check all cores to see if they're really done.
        for (size_t i = 0; i < m_procs.size(); ++i)
        {
            if (!m_procs[i]->IsIdle())
            {
                state = STATE_DEADLOCK;
                break;
            }
        }
    }

    if (state == STATE_DEADLOCK)
    {
        cerr << "Replaying the last cycle:" << endl;

        int savemode = GetDebugMode();
        SetDebugMode(-1);
        state = GetKernel().Step(1);
        SetDebugMode(savemode);

        ostringstream ss;
        ss << "Stalled processes:" << endl;

        // See how many processes are in each of the states
        unsigned int num_stalled = 0, num_running = 0;

        for (const Clock* clock = m_kernel.GetActiveClocks(); clock != NULL; clock = clock->GetNext())
        {
            for (const Process* process = clock->GetActiveProcesses(); process != NULL; process = process->GetNext())
            {
                switch (process->GetState())
                {
                case STATE_DEADLOCK: 
                    ss << "  " << process->GetName() << endl;
                    ++num_stalled; 
                    break;
                case STATE_RUNNING:  
                    ++num_running; 
                    break;
                default: 
                    assert(false); 
                    break;
                }
            }
        }

        ss << "Suspended registers:" << endl;

        unsigned int num_regs = 0;
        for (size_t i = 0; i < m_procs.size(); ++i)
        {
            unsigned suspended = m_procs[i]->GetNumSuspendedRegisters();
            if (suspended)
                ss << "  " << m_procs[i]->GetFQN() << ": " << suspended << endl;
            num_regs += suspended;
        }

        ss << endl
           << "Deadlock! (at cycle " << m_kernel.GetCycleNo() << ')' << endl
           << "(" << num_stalled << " processes stalled;  " << num_running << " processes running; "
           << num_regs << " registers waited on)";
        throw DeadlockException(ss.str());
    }

    if (state == STATE_ABORTED)
    {
        if (m_breakpoints.NewBreaksDetected())
        {
            ostringstream ss;
            m_breakpoints.ReportBreaks(ss);
            throw runtime_error(ss.str());
        }
        else
            // The simulation was aborted, because the user interrupted it.
            throw runtime_error("Interrupted!");
    }
}

void MGSystem::Disassemble(MemAddr addr, size_t sz) const
{
    ostringstream cmd;

    cmd << m_objdump_cmd << " -d -r --prefix-addresses --show-raw-insn --start-address=" << addr
        << " --stop-address=" << addr + sz << " " << m_program;
    clog << "Running " << cmd.str() << "..." << endl;
    system(cmd.str().c_str());
}

MGSystem::MGSystem(Config& config, const string& program,
                   const string& symtable,
                   const vector<pair<RegAddr, RegValue> >& regs,
                   const vector<pair<RegAddr, string> >& loads,
                   bool quiet, bool doload)
    : m_kernel(m_symtable, m_breakpoints),
      m_clock(m_kernel.CreateClock(config.getValue<unsigned long>("CoreFreq"))),
      m_root("system", m_clock),
      m_breakpoints(m_kernel),
      m_program(program),
      m_config(config)
{
    PSize numProcessors = m_config.getValue<PSize>("NumProcessors");

    const size_t numProcessorsPerFPU = config.getValue<size_t>("NumProcessorsPerFPU");
    const PSize  numFPUs             = (numProcessors + numProcessorsPerFPU - 1) / numProcessorsPerFPU;

    string memory_type = config.getValue<string>("MemoryType");
    transform(memory_type.begin(), memory_type.end(), memory_type.begin(), ::toupper);

    Clock& memclock = m_kernel.CreateClock(config.getValue<size_t>("MemoryFreq"));

    if (memory_type == "SERIAL") {
        SerialMemory* memory = new SerialMemory("memory", m_root, memclock, config);
        m_memory = memory;
        m_memorytype = MEMTYPE_SERIAL;
    } else if (memory_type == "PARALLEL") {
        ParallelMemory* memory = new ParallelMemory("memory", m_root, memclock, config);
        m_memory = memory;
        m_memorytype = MEMTYPE_PARALLEL;
    } else if (memory_type == "BANKED") {
        BankedMemory* memory = new BankedMemory("memory", m_root, memclock, config);
        m_memory = memory;
        m_memorytype = MEMTYPE_BANKED;
    } else if (memory_type == "RANDOMBANKED") {
        RandomBankedMemory* memory = new RandomBankedMemory("memory", m_root, memclock, config);
        m_memory = memory;
        m_memorytype = MEMTYPE_RANDOMBANKED;
    } else if (memory_type == "COMA") {
        COMA* memory = new COMA("memory", m_root, memclock, config);
        m_memory = memory;
        m_memorytype = MEMTYPE_COMA_ML;
    } else if (memory_type == "ZLCOMA") {
        ZLCOMA* memory = new ZLCOMA("memory", m_root, memclock, config);
        m_memory = memory;
        m_memorytype = MEMTYPE_COMA_ZL;
    } else {
        throw runtime_error("Unknown memory type: " + memory_type);
    }

    // Create the I/O Buses
    const size_t numIOBuses = config.getValue<size_t>("NumIOBuses");
    m_iobuses.resize(numIOBuses);
    for (size_t b = 0; b < numIOBuses; ++b)
    {
        stringstream ss;
        ss << "iobus" << b;
        string name = ss.str();

        string bus_type = config.getValue<string>(m_root, name + ".Type");
        Clock& ioclock = m_kernel.CreateClock(config.getValue<unsigned long>(m_root, name + ".Freq"));

        if (bus_type == "NULLIO") {
            m_iobuses[b] = new NullIO(name, m_root, ioclock);
        } else {
            throw runtime_error("Unknown I/O bus type for " + name + ": " + bus_type);
        }
    }

    // Create the FPUs
    m_fpus.resize(numFPUs);
    for (size_t f = 0; f < numFPUs; ++f)
    {
        stringstream name;
        name << "fpu" << f;
        m_fpus[f] = new FPU(name.str(), m_root, m_clock, config, numProcessorsPerFPU);
    }

    // Create processor grid
    m_procs.resize(numProcessors);
    for (size_t i = 0; i < numProcessors; ++i)
    {
        FPU& fpu = *m_fpus[i / numProcessorsPerFPU];

        stringstream ss;
        ss << "cpu" << i;
        string name = ss.str();

        IIOBus* iobus = NULL;
        if (config.getValue<bool>(m_root, name + ".EnableIO", false)) // I/O disabled unless specified
        {
            size_t busid = config.getValue<size_t>(m_root, name + ".BusID");
            if (busid >= m_iobuses.size())
            {
                throw runtime_error("Processor " + name + " set to connect to non-existent bus");
            }

            m_procbusmapping[i] = busid;
            iobus = m_iobuses[busid];
        }

        m_procs[i]   = new Processor(name, m_root, m_clock, i, m_procs, *m_memory, fpu, iobus, config);
    }

    // Create the I/O devices
    size_t numIODevices = config.getValue<size_t>("NumIODevices");

    m_devices.resize(numIODevices);
    for (size_t i = 0; i < numIODevices; ++i)
    {
        stringstream ss;
        ss << "iodev" << i;
        string name = ss.str();

        size_t busid = config.getValue<size_t>(m_root, name + ".BusID");

        if (busid >= m_iobuses.size())
        {
            throw runtime_error("Device " + name + " set to connect to non-existent bus");
        }
        
        IIOBus& iobus = *m_iobuses[busid];

        size_t devid = config.getValue<size_t>(m_root, name + ".DeviceID");
        string dev_type = config.getValue<string>(m_root, name + ".Type");

        if (dev_type == "LCD") {
            LCD *lcd = new LCD(name, m_root, iobus, devid, config);
            iobus.RegisterClient(devid, *lcd);
            m_devices[i] = lcd;
        } else if (dev_type == "RTC") {
            Clock& rtcclock = m_kernel.CreateClock(config.getValue<size_t>(m_root, name + ".RTCUpdateFreq"));
            RTC *rtc = new RTC(name, m_root, rtcclock, iobus, devid, config);
            m_devices[i] = rtc;
        } else if (dev_type == "GFX") {
            size_t fbdevid = config.getValue<size_t>(m_root, name + ".GfxFrameBufferDeviceID", devid + 1);
            Display *disp = new Display(name, m_root, iobus, devid, fbdevid, config);
            m_devices[i] = disp;
        } else {
            throw runtime_error("Unknown I/O device type: " + dev_type);
        }

    }

    // Load the program into memory
    pair<MemAddr, bool> progdesc = make_pair(0, false);
    if (doload)
        progdesc = LoadProgram(m_memory, program, quiet);

    // Connect processors in the link
    for (size_t i = 0; i < numProcessors; ++i)
    {
        Processor* prev = (i == 0)                 ? NULL : m_procs[i - 1];
        Processor* next = (i == numProcessors - 1) ? NULL : m_procs[i + 1];
        m_procs[i]->Initialize(prev, next, progdesc.first, progdesc.second);
    }

    if (doload && !m_procs.empty())
    {
        // Fill initial registers
        for (size_t i = 0; i < regs.size(); ++i)
        {
            m_procs[0]->WriteRegister(regs[i].first, regs[i].second);
        }

        // Load data files
        for (size_t i = 0; i < loads.size(); ++i)
        {
            RegValue value;
            value.m_state   = RST_FULL;

            pair<MemAddr, size_t> p = LoadDataFile(m_memory, loads[i].second, quiet);
            value.m_integer = p.first;

            m_procs[0]->WriteRegister(loads[i].first, value);
            m_symtable.AddSymbol(p.first, loads[i].second, p.second);
            m_inputfiles.push_back(loads[i].second);
        }

        // Load configuration
        // Store the address in local #2
        RegValue value;
        value.m_state   = RST_FULL;
        value.m_integer = WriteConfiguration();
        m_procs[0]->WriteRegister(MAKE_REGADDR(RT_INTEGER, 2), value);

#if defined(TARGET_MTALPHA)
        // The Alpha expects the function address in $27
        value.m_integer = progdesc.first;
        m_procs[0]->WriteRegister(MAKE_REGADDR(RT_INTEGER, 27), value);
#endif
    }

    // Set program debugging per default
    m_kernel.SetDebugMode(Kernel::DEBUG_PROG);

    // Load symbol table
    if (doload && !symtable.empty())
    {
        ifstream in(symtable.c_str(), ios::in);
        m_symtable.Read(in);
    }

    // Find objdump command
#if defined(TARGET_MTALPHA)
    const char *default_objdump = "mtalpha-linux-gnu-objdump";
    const char *objdump_var = "MTALPHA_OBJDUMP";
#endif
#if defined(TARGET_MTSPARC)
    const char *default_objdump = "mtsparc-linux-gnu-objdump";
    const char *objdump_var = "MTSPARC_OBJDUMP";
#endif
    const char *v = getenv(objdump_var);
    if (!v) v = default_objdump;
    m_objdump_cmd = v;

    if (!quiet)
    {
        static char const qual[] = {'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};
        unsigned long long freq = m_kernel.GetMasterFrequency();
        unsigned int q;
        for (q = 0; freq % 1000 == 0 && q < sizeof(qual)/sizeof(qual[0]); ++q)
        {
            freq /= 1000;
        }
        cout << "Created Microgrid; simulation running at " << dec << freq << " " << qual[q] << "Hz" << endl;
    }
}

MGSystem::~MGSystem()
{
    for (size_t i = 0; i < m_iobuses.size(); ++i)
    {
        delete m_iobuses[i];
    }
    for (size_t i = 0; i < m_devices.size(); ++i)
    {
        delete m_devices[i];
    }
    for (size_t i = 0; i < m_procs.size(); ++i)
    {
        delete m_procs[i];
    }
    for (size_t i = 0; i < m_fpus.size(); ++i)
    {
        delete m_fpus[i];
    }
    // for (size_t i = 0; i < m_lcds.size(); ++i)
    // {
    //     delete m_lcds[i];
    // }
    delete m_memory;
}
