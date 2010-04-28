#include "MGSystem.h"

#ifdef ENABLE_COMA_ZL
# include "CMLink.h"
# include "coma/simlink/th.h"
# include "coma/simlink/linkmgs.h"
#else
# include "SerialMemory.h"
# include "ParallelMemory.h"
# include "BankedMemory.h"
# include "RandomBankedMemory.h"
# include "coma/COMA.h"
#endif

#include "loader.h"

// For Tokenize:
#include "simreadline.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <cmath>

using namespace Simulator;
using namespace std;
#ifdef ENABLE_COMA_ZL
using namespace MemSim;
#endif

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

#define CONFTAG_ARCH_V1    1
#define CONFTAG_TIMINGS_V1 2
#define CONFTAG_CACHE_V1   3
#define CONFTAG_CONC_V1    4
#define CONFTAG_LAYOUT_V1  5
#define MAKE_TAG(Type, Size) (uint32_t)(((Type) << 16) | ((Size) & 0xffff))

struct ConfWords
{
    vector<uint32_t> data;
    
    ConfWords& operator<<(uint32_t val) 
    {
        uint32_t repr;
        SerializeRegister(RT_INTEGER, val, &repr, sizeof(repr));
        data.push_back(repr);
        return *this;
    }
};

MemAddr MGSystem::WriteConfiguration(const Config& config)
{

    uint32_t cl_sz = config.getInteger<uint32_t>("CacheLineSize", 0);

    ConfWords words;

    // configuration words for architecture type v1

    words << MAKE_TAG(CONFTAG_ARCH_V1, 4)
          << 1 // simulated microgrid
#if TARGET_ARCH == ARCH_ALPHA
          << 1 // alpha
#else
          << 2 // sparc
#endif    
          << m_fpus.size()
          << m_memorytype

    
    // timing words v1

          << MAKE_TAG(CONFTAG_TIMINGS_V1, 2)
          << config.getInteger<uint32_t>("CoreFreq", 0)
          << ((m_memorytype == MEMTYPE_COMA_ZL || m_memorytype == MEMTYPE_COMA_ML) ? 
              config.getInteger<uint32_t>("DDRMemoryFreq", 0) 
              * config.getInteger<uint32_t>("NumRootDirectories", 0)
              * 3 /* DDR3 = triple rate */ 
              * 8 /* 64 bit = 8 bytes per transfer */
              : 0 /* no timing information if memory system is not COMA */)
        
    // cache parameter words v1
    
          << MAKE_TAG(CONFTAG_CACHE_V1, 5)
          << cl_sz
          << (cl_sz 
              * config.getInteger<uint32_t>("ICacheAssociativity", 0)
              * config.getInteger<uint32_t>("ICacheNumSets", 0))
          << (cl_sz
              * config.getInteger<uint32_t>("DCacheAssociativity", 0)
              * config.getInteger<uint32_t>("DCacheNumSets", 0));
    if (m_memorytype == MEMTYPE_COMA_ZL || m_memorytype == MEMTYPE_COMA_ML)
        words << (m_procs.size()
                  / config.getInteger<uint32_t>("NumProcessorsPerCache", 0)) // FIXME: COMA?
              << (cl_sz
                  * config.getInteger<uint32_t>("L2CacheAssociativity", 0)
                  * config.getInteger<uint32_t>("L2CacheNumSets", 0));
    else
        words << 0 << 0;

    // concurrency resources v1
    
    words << MAKE_TAG(CONFTAG_CONC_V1, 4)
          << config.getInteger<uint32_t>("NumFamilies", 0)
          << config.getInteger<uint32_t>("NumThreads", 0)
          << config.getInteger<uint32_t>("NumIntRegisters", 0)
          << config.getInteger<uint32_t>("NumFltRegisters", 0)

        ;

    // place layout v1

    const vector<PSize>& placeSizes = config.getIntegerList<PSize>("NumProcessors");

    words << MAKE_TAG(CONFTAG_LAYOUT_V1, m_procs.size() + 1)
          << m_procs.size();

    // Store the cores, per place
    PSize first = 0;
    for (size_t p = 0; p < placeSizes.size(); ++p)
    {
        PSize placeSize = placeSizes[p];
        for (size_t i = 0; i < placeSize; ++i)
        {
            PSize pid = first + i;
            words << ((p << 16) | (pid << 0));
        }
        first += placeSize;
    }

    // after last block
    words << 0 << 0;

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
        op += m_procs[i]->GetOp();
    }
    return op;
}

uint64_t MGSystem::GetFlop() const
{
    uint64_t flop = 0;
    for (size_t i = 0; i < m_procs.size(); ++i) {
        flop += m_procs[i]->GetFlop();
    }
    return flop;
}

void MGSystem::PrintMemoryStatistics(std::ostream& os) const {
    uint64_t nr = 0, nrb = 0, nw = 0, nwb = 0;

    for (size_t i = 0; i < m_procs.size(); ++i) {
        m_procs[i]->CollectMemOpStatistics(nr, nw, nrb, nwb);
    }

    os << nr << "\t# number of completed load insns." << endl
       << nrb << "\t# number of bytes loaded by completed loads" << endl
       << nw << "\t# number of completed store insns." << endl
       << nwb << "\t# number of bytes stored by completed stores" << endl;

    m_memory->GetMemoryStatistics(nr, nw, nrb, nwb);
    os << nr << "\t# number of load reqs. from the ext. mem. interface" << endl
       << nrb << "\t# number of bytes loaded from the ext. mem. interface" << endl
       << nw << "\t# number of store reqs. to the ext. mem. interface" << endl
       << nwb << "\t# number of bytes stored to the ext. mem. interface" << endl;

}

void MGSystem::PrintState(const vector<string>& arguments) const
{
    typedef map<string, RunState> StateMap;

    StateMap   states;
    streamsize length = 0;

    // This should be all non-idle processes
    for (const Process* process = m_kernel.GetActiveProcesses(); process != NULL; process = process->GetNext())
    {
        const std::string name = process->GetName();
        states[name] = process->GetState();
        length = std::max(length, (streamsize)name.length());
    }

    cout << left << setfill(' ');
    for (StateMap::const_iterator p = states.begin(); p != states.end(); ++p)
    {
        cout << setw(length) << p->first << ": ";
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
    cout << endl;

    int width = (int)log10(m_procs.size()) + 1;
    for (size_t i = 0; i < m_procs.size(); ++i)
    {
        if (!m_procs[i]->IsIdle()) {
            cout << "Processor " << dec << right << setw(width) << i << ": non-empty" << endl;
        }
    }
}

void MGSystem::PrintRegFileAsyncPortActivity(std::ostream& os) const
{
    float avg  = 0;
    float amax = 0.0f;
    float amin = 1.0f;
    for (size_t i = 0; i < m_procs.size(); ++i) {
        float a = m_procs[i]->GetRegFileAsyncPortActivity();
        amax = max(amax, a);
        amin = min(amin, a);
        avg += a;
    }
    avg /= (float)m_procs.size();
    os << avg << "\t# average reg. file async port activity" << endl
       << amin << "\t# min reg. file async port activity" << endl
       << amax << "\t# max reg. file async port activity" << endl;
}

void MGSystem::PrintPipelineEfficiency(std::ostream& os) const
{
    float avg  = 0;
    float amax = 0.0f;
    float amin = 1.0f;
    size_t num = 0;
    for (size_t i = 0; i < m_procs.size(); ++i) {
        float a = m_procs[i]->GetPipelineEfficiency();
        if (a > 0)
        {
            amax = max(amax, a);
            amin = min(amin, a);
            avg += a;
            num++;
        }
    }
    avg /= (float)num;
    os << avg << "\t# average pipeline efficiency" << endl
       << amin << "\t# min pipeline efficiency" << endl
       << amax << "\t# max pipeline efficiency" << endl;
}

void MGSystem::PrintPipelineIdleTime(std::ostream& os) const
{
    float    avg    = 0;
    uint64_t amax   = 0;
    uint64_t amin   = numeric_limits<uint64_t>::max();
    for (size_t i = 0; i < m_procs.size(); ++i) {
        float a = (float)m_procs[i]->GetAvgPipelineIdleTime();
        amax    = max(amax, m_procs[i]->GetMaxPipelineIdleTime() );
        amin    = min(amin, m_procs[i]->GetMinPipelineIdleTime() );
        avg += a;
    }
    avg /= (float)m_procs.size();
    os << avg << "\t# average pipeline idle cycles per core" << endl
       << amin << "\t# min pipeline idle cycles per core (overall min)" << endl
       << amax << "\t# max pipeline idle cycles per core (overall max)" << endl;
}

void MGSystem::PrintAllFamilyCompletions(std::ostream& os) const
{
    for (PSize i = 0; i < m_procs.size(); i++) {
        CycleNo last = m_procs[i]->GetLocalFamilyCompletion();
        if (last != 0)
            os << m_procs[i]->GetLocalFamilyCompletion()
               << "\t# cycle counter at last family completion on core " << i
               << endl;
    }
}

void MGSystem::PrintFamilyCompletions(std::ostream& os) const
{
    CycleNo first = numeric_limits<CycleNo>::max();
    CycleNo last  = 0;
    for (size_t i = 0; i < m_procs.size(); ++i) {
        CycleNo cycle = m_procs[i]->GetLocalFamilyCompletion();
        if (cycle != 0)
        {
            first = min(first, cycle);
            last  = max(last,  cycle);
        }
    }
    os << first << "\t# cycle counter at first family completion" << endl
       << last << "\t# cycle counter at last family completion" << endl;
}

void MGSystem::PrintAllStatistics(std::ostream& os) const
{
    os << dec;
    os << GetKernel().GetCycleNo() << "\t# cycle counter" << endl
       << GetOp() << "\t# total executed instructions" << endl
       << GetFlop() << "\t# total issued fp instructions" << endl;
    PrintMemoryStatistics(os);
    PrintRegFileAsyncPortActivity(os);
    PrintPipelineIdleTime(os);
    PrintPipelineEfficiency(os);
    PrintFamilyCompletions(os);
    PrintAllFamilyCompletions(os);
#ifdef ENABLE_COMA_ZL
    os << LinkMGS::s_oLinkConfig.m_nProcs << "\t# COMA: number of connected cores" << endl
       << LinkMGS::s_oLinkConfig.m_nProcessorsPerCache << "\t# COMA: number of processors per L2 cache" << endl
       << LinkMGS::s_oLinkConfig.m_nCachesPerDirectory << "\t# COMA: number of L2 caches per directory" << endl
       << g_uMemoryAccessesL << "\t# COMA: number of DDR load reqs (total)" << endl
       << g_uMemoryAccessesS << "\t# COMA: number of DDR store reqs (total)" << endl
       << g_uHitCountL << "\t# COMA: number of L2 cache load hits (total)" << endl
       << g_uHitCountS << "\t# COMA: number of L2 cache store hits (total)" << endl
       << g_uTotalL
       << "\t# COMA: number of mem. load reqs received by L2 caches from cores (total)" << endl
       << g_uTotalS
       << "\t# COMA: number of mem. store reqs received by L2 caches from cores (total)" << endl
       << g_fLatency
       << "\t# COMA: accumulated latency of mem. reqs (total, in seconds)" << endl
       << ((double)g_uAccessDelayL)/g_uAccessL
       << "\t# COMA: average latency of mem. loads (in cycles)" << endl
       << g_uAccessL
       << "\t# COMA: number of mem. load reqs sent from cores (total)" << endl
       << ((double)g_uAccessDelayS)/g_uAccessS
       << "\t# COMA: average latency of mem. stores (in cycles)" << endl
       << g_uAccessS
       << "\t# COMA: number of mem. store reqs sent from cores (total)" << endl
       <<  ((double)g_uConflictDelayL)/g_uConflictL
       << "\t# COMA: average latency of mem. load conflicts (in cycles)" << endl
       << g_uConflictL
       << "\t# COMA: number of mem. load conflicts from cores (total)" << endl
       << g_uConflictAddL
       << "\t# COMA: number of load conflicts in L2 caches (total)" << endl
       << ((double)g_uConflictDelayS)/g_uConflictS
       << "\t# COMA: average latency of mem. store conflicts (in cycles)" << endl
       << g_uConflictS
       << "\t# COMA: number of mem. store conflicts from cores (total)" << endl
       << g_uConflictAddS
       << "\t# COMA: number of store conflicts in L2 caches (total)" << endl
       <<  g_uProbingLocalLoad
       << "\t# COMA: number of L2 hits by reusing invalidated cache lines (total)" << endl;
#endif
}

// Find a component in the system given its path
// Returns NULL when the component is not found
Object* MGSystem::GetComponent(const string& path)
{
    Object* cur = this;
    vector<string> names = Tokenize(path, ".");
    for (vector<string>::iterator p = names.begin(); cur != NULL && p != names.end(); ++p)
    {
        transform(p->begin(), p->end(), p->begin(), ::toupper);

        Object* next = NULL;
        for (unsigned int i = 0; i < cur->GetNumChildren(); ++i)
        {
            Object* child = cur->GetChild(i);
            string name   = child->GetName();
            transform(name.begin(), name.end(), name.begin(), ::toupper);
            if (name == *p)
            {
                next = child;
                break;
            }
        }
        cur = next;
    }
    return cur;
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
        // See how many processes are in each of the states
        unsigned int num_stalled = 0, num_running = 0;
        
        for (const Process* process = m_kernel.GetActiveProcesses(); process != NULL; process = process->GetNext())
        {
            switch (process->GetState())
            {
            case STATE_DEADLOCK: ++num_stalled; break;
            case STATE_RUNNING:  ++num_running; break;
            default:             assert(false); break;
            }
        }
        
        unsigned int num_regs = 0;
        for (size_t i = 0; i < m_procs.size(); ++i)
        {
            num_regs += m_procs[i]->GetNumSuspendedRegisters();
        }

        ostringstream ss;
        ss << "Deadlock!" << endl
           << "(" << num_stalled << " processes stalled;  " << num_running << " processes running; "
           << num_regs << " registers waited on)";
        throw runtime_error(ss.str());
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

string MGSystem::GetSymbol(MemAddr addr) const
{
    return m_symtable[addr];
}

void MGSystem::PrintAllSymbols(ostream& o, const string& pat) const
{
    m_symtable.Write(o, pat);
}

void MGSystem::Disassemble(MemAddr addr, size_t sz) const
{
    ostringstream cmd;

    cmd << m_objdump_cmd << " -d -r --prefix-addresses --show-raw-insn --start-address=" << addr
        << " --stop-address=" << addr + sz << " " << m_program;
    system(cmd.str().c_str());
}

MGSystem::MGSystem(const Config& config, Display& display, const string& program,
                   const string& symtable,
                   const vector<pair<RegAddr, RegValue> >& regs,
                   const vector<pair<RegAddr, string> >& loads,
                   bool quiet)
    : Object("system", m_kernel),
      m_breakpoints(m_kernel),
      m_kernel(display, m_symtable, m_breakpoints),
      m_program(program)
{
    const vector<PSize> placeSizes = config.getIntegerList<PSize>("NumProcessors");
    const size_t numProcessorsPerFPU_orig = max<size_t>(1, config.getInteger<size_t>("NumProcessorsPerFPU", 1));

    // Validate the #cores/FPU
    size_t numProcessorsPerFPU = numProcessorsPerFPU_orig;
    for (; numProcessorsPerFPU > 1; --numProcessorsPerFPU)
    {
        size_t i;
        for (i = 0; i < placeSizes.size(); ++i) {
            if (placeSizes[i] % numProcessorsPerFPU != 0) {
                break;
            }
        }
        if (i == placeSizes.size()) break;
    }

    if ((numProcessorsPerFPU != numProcessorsPerFPU_orig) && !quiet)
        std::cerr << "Warning: #cores in at least one place cannot be divided by "
                  << numProcessorsPerFPU_orig
                  << " cores/FPU" << std::endl
                  << "Value has been adjusted to "
                  << numProcessorsPerFPU
                  << " cores/FPU" << std::endl;


    PSize numProcessors = 0;
    size_t numFPUs      = 0;
    for (size_t i = 0; i < placeSizes.size(); ++i) {
        if (placeSizes[i] % numProcessorsPerFPU != 0) {
            throw runtime_error("#cores in at least one place cannot be divided by #cores/FPU");
        }
        numProcessors += placeSizes[i];
        numFPUs       += placeSizes[i] / numProcessorsPerFPU;
    }

#ifdef ENABLE_COMA_ZL
    m_objects.resize(numProcessors * 2 + numFPUs);
    CMLink** &m_pmemory = (CMLink**&)this->m_pmemory;
    m_pmemory = new CMLink*[LinkMGS::s_oLinkConfig.m_nProcs];
    m_memorytype = MEMTYPE_COMA_ZL;
#else
    std::string memory_type = config.getString("MemoryType", "");
    std::transform(memory_type.begin(), memory_type.end(), memory_type.begin(), ::toupper);

    m_objects.resize(numProcessors + numFPUs + 1);
    if (memory_type == "SERIAL") {
        SerialMemory* memory = new SerialMemory("memory", *this, config);
        m_objects.back() = memory;
        m_memory = memory;
        m_memorytype = MEMTYPE_SERIAL;
    } else if (memory_type == "PARALLEL") {
        ParallelMemory* memory = new ParallelMemory("memory", *this, config);
        m_objects.back() = memory;
        m_memory = memory;
        m_memorytype = MEMTYPE_PARALLEL;
    } else if (memory_type == "BANKED") {
        BankedMemory* memory = new BankedMemory("memory", *this, config);
        m_objects.back() = memory;
        m_memory = memory;
        m_memorytype = MEMTYPE_BANKED;
    } else if (memory_type == "RANDOMBANKED") {
        RandomBankedMemory* memory = new RandomBankedMemory("memory", *this, config);
        m_objects.back() = memory;
        m_memory = memory;
        m_memorytype = MEMTYPE_RANDOMBANKED;
    } else if (memory_type == "COMA") {
        COMA* memory = new COMA("memory", *this, config);
        m_objects.back() = memory;
        m_memory = memory;
        m_memorytype = MEMTYPE_COMA_ML;
    } else {
        throw std::runtime_error("Unknown memory type specified in configuration");
    }
#endif

    // Create the FPUs
    m_fpus.resize(numFPUs);
    for (size_t f = 0; f < numFPUs; ++f)
    {
        stringstream name;
        name << "fpu" << f;
        m_fpus[f] = new FPU(name.str(), *this, config, numProcessorsPerFPU);
    }

    // Create processor grid
    m_procs.resize(numProcessors);
    m_places.resize(placeSizes.size());


    PSize first = 0;
    for (size_t p = 0; p < placeSizes.size(); ++p)
    {
        m_places[p] = new PlaceInfo(m_kernel, placeSizes[p]);
        for (size_t i = 0; i < m_places[p]->m_size; ++i)
        {
            PSize pid = (first + i);
            FPU&  fpu = *m_fpus[pid / numProcessorsPerFPU];

            stringstream name;
            name << "cpu" << pid;
#ifdef ENABLE_COMA_ZL
            stringstream namem;
            namem << "memory" << pid;
            m_pmemory[pid] = new CMLink(namem.str(), *this, config, g_pLinks[i]);
            if (pid == 0)
                m_memory = m_pmemory[0];
            m_procs[pid]   = new Processor(name.str(), *this, pid, i, m_procs, m_procs.size(), 
                                           *m_places[p], *m_pmemory[pid], fpu, config);
            m_pmemory[pid]->SetProcessor(m_procs[pid]);
            m_objects[pid+numProcessors] = m_pmemory[pid];
#else
            m_procs[pid]   = new Processor(name.str(), *this, pid, i, m_procs, m_procs.size(), 
                                           *m_places[p], *m_memory, fpu, config);
#endif
            m_objects[pid] = m_procs[pid];
        }
        first += m_places[p]->m_size;
    }

    // Load the program into memory
    std::pair<MemAddr, bool> progdesc = LoadProgram(m_memory, program, quiet);

    // Connect processors in rings
    first = 0;
    for (size_t p = 0; p < placeSizes.size(); ++p)
    {
        PSize placeSize = placeSizes[p];
        for (size_t i = 0; i < placeSize; ++i)
        {
            PSize pid = (first + i);
            LPID prev = (i + placeSize - 1) % placeSize;
            LPID next = (i + 1) % placeSize;
            m_procs[pid]->Initialize(*m_procs[first + prev], *m_procs[first + next], progdesc.first, progdesc.second);
        }
        first += placeSize;
    }

    if (!m_procs.empty())
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
        }

        // Load configuration
        // Store the address in local #2
        RegValue value;
        value.m_state   = RST_FULL;
        value.m_integer = WriteConfiguration(config);
        m_procs[0]->WriteRegister(MAKE_REGADDR(RT_INTEGER, 2), value);

#if TARGET_ARCH == ARCH_ALPHA
        // The Alpha expects the function address in $27
        value.m_integer = progdesc.first;
        m_procs[0]->WriteRegister(MAKE_REGADDR(RT_INTEGER, 27), value);
#endif
    }

    // Set program debugging per default
    m_kernel.SetDebugMode(Kernel::DEBUG_PROG);

    // Load symbol table
    if (!symtable.empty()) 
    {
        ifstream in(symtable.c_str(), ios::in);
        m_symtable.Read(in);
    }

    // Find objdump command
#if TARGET_ARCH == ARCH_ALPHA
    const char *default_objdump = "mtalpha-linux-gnu-objdump";
    const char *objdump_var = "MTALPHA_OBJDUMP";
#endif
#if TARGET_ARCH == ARCH_SPARC
    const char *default_objdump = "mtsparc-leon-linux-objdump";
    const char *objdump_var = "MTSPARC_OBJDUMP";
#endif
    const char *v = getenv(objdump_var);
    if (!v) v = default_objdump;
    m_objdump_cmd = v;
}

MGSystem::~MGSystem()
{
    for (size_t i = 0; i < m_procs.size(); ++i)
    {
        delete m_procs[i];
    }
    for (size_t i = 0; i < m_places.size(); ++i)
    {
        delete m_places[i];
    }
    for (size_t i = 0; i < m_fpus.size(); ++i)
    {
        delete m_fpus[i];
    }
    delete m_memory;
}
