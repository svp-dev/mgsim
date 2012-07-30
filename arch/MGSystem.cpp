#include "MGSystem.h"

#include "mem/SerialMemory.h"
#include "mem/ParallelMemory.h"
#include "mem/BankedMemory.h"
#include "mem/DDRMemory.h"
#include "mem/coma/COMA.h"
#include "mem/zlcoma/COMA.h"

#include "arch/dev/NullIO.h"
#include "arch/dev/LCD.h"
#include "arch/dev/RTC.h"
#include "arch/dev/Display.h"
#include "arch/dev/ActiveROM.h"
#include "arch/dev/Selector.h"
#include "arch/dev/SMC.h"
#include "arch/dev/UART.h"
#include "arch/dev/RPC.h"

// maybe replace the following if the host-guest syscall API ever
// changes.
#include "arch/dev/RPC_unix.h"

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
std::basic_ostream<_CharT, _Traits>& operator<<(std::basic_ostream<_CharT, _Traits>&os, my_iomanip_i /*unused*/)
{
    os << std::left << std::fixed << std::setprecision(2) << std::setw(9);
    return os;
}

struct my_iomanip_f { };
template<typename _CharT, typename _Traits>
std::basic_ostream<_CharT, _Traits>& operator<<(std::basic_ostream<_CharT, _Traits>&os, my_iomanip_f /*unused*/)
{
    os << std::left << std::scientific << std::setprecision(6) << std::setw(12);
    return os;
}

struct my_iomanip_p { };
template<typename _CharT, typename _Traits>
std::basic_ostream<_CharT, _Traits>& operator<<(std::basic_ostream<_CharT, _Traits>&os, my_iomanip_p /*unused*/)
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

static string StringReplace(string arg, string pat, string repl)
{
    string res;
    for (size_t i = 0; i < arg.size(); ++i)
    {
        if (arg.compare(i, pat.size(), pat) == 0)
        {
            res += repl;
            i += pat.size() - 1;
        }
        else
            res += arg[i];
    }
    return res;
}

void MGSystem::PrintProcesses(ostream& out, const string& pat) const
{
    const std::set<const Process*>& allprocs = Process::GetAllProcesses();
    for (std::set<const Process*>::const_iterator i = allprocs.begin(); i != allprocs.end(); ++i)
    {
        std::string name = (*i)->GetName();
        if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), 0))
        {
            out << name << endl;
        }
    }
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
                << StringReplace(GetClassName(typeid(*child)), "Simulator::", "") << endl;

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
        types[j] = I; c[i][j++].i = pl.GetStalls();
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
       << fi << "plstalls" << sep
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
       << "# plstalls: cumulative number of corecycles that (a part of) the pipeline stalled" << endl
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

void MGSystem::PrintState(const vector<string>& /*unused*/) const
{
    // This should be all non-idle processes
    for (const Clock* clock = m_kernel.GetActiveClocks(); clock != NULL; clock = clock->GetNext())
    {
        if (clock->GetActiveProcesses() != NULL || clock->GetActiveStorages() != NULL || clock->GetActiveArbitrators() != NULL)
        {
            cout << endl
                 << "Connected to a " << clock->GetFrequency() << " MHz clock (next tick at cycle " << dec << clock->GetNextTick() << "):" << endl;

            if (clock->GetActiveProcesses() != NULL)
            {
                cout << "- the following processes are powered:" << endl;
                for (const Process* process = clock->GetActiveProcesses(); process != NULL; process = process->GetNext())
                {
                    cout << "  - " << process->GetName() << " (";
                    switch (process->GetState())
                    {
                    case STATE_IDLE:     assert(0); break;
                    case STATE_ACTIVE:   cout << "active"; break;
                    case STATE_DEADLOCK: cout << "stalled"; break;
                    case STATE_RUNNING:  cout << "running"; break;
                    case STATE_ABORTED:  assert(0); break;
                    }
                    cout << ')' << endl;
                }
            }

            if (clock->GetActiveStorages() != NULL)
            {
                cout << "- the following storages need updating:" << endl;
                for (const Storage* storage = clock->GetActiveStorages(); storage != NULL; storage = storage->GetNext())
                {
                    cout << "  - " << storage->GetFQN() << endl;
                }
            }

            if (clock->GetActiveArbitrators() != NULL)
            {
                cout << "- the following arbitrators need updating:" << endl;
                for (const Arbitrator* arbitrator = clock->GetActiveArbitrators(); arbitrator != NULL; arbitrator = arbitrator->GetNext())
                {
                    cout << "  - " << arbitrator->GetFQN() << endl;
                }
            }
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
    os << GetKernel().GetCycleNo() << "\t# master cycle counter" << endl
       << m_clock.GetCycleNo() << "\t# core cycle counter" << endl
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
        // An idle state might actually be deadlock if there's a
        // suspended thread.  So check all cores to see if they're
        // really done.
        for (size_t i = 0; i < m_procs.size(); ++i)
        {
            if (!m_procs[i]->IsIdle())
            {
                state = STATE_DEADLOCK;
                break;
            }
        }

        // If all cores are done, but there are still some remaining
        // processes, and all the remaining processes are stalled,
        // then there is a deadlock too.  However since the kernel
        // state is idle, there cannot be any running process left. So
        // either there are no processes at all, or they are all
        // stalled. Deadlock only exists in the latter case, so
        // we only check for the existence of an active process.
        if (state != STATE_DEADLOCK)
            for (const Clock* clock = m_kernel.GetActiveClocks(); clock != NULL; clock = clock->GetNext())
            {
                if (clock->GetActiveProcesses() != NULL)
                {
                    state = STATE_DEADLOCK;
                    break;
                }
            }
    }

    if (state == STATE_DEADLOCK)
    {
        cerr << "Deadlock at cycle " << GetKernel().GetCycleNo() << "; replaying the last cycle:" << endl;

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
        << " --stop-address=" << addr + sz << " " << m_bootrom->GetProgramName();
    clog << "Running " << cmd.str() << "..." << endl;
    system(cmd.str().c_str());
}

MGSystem::MGSystem(Config& config,
                   const string& symtable,
                   const vector<pair<RegAddr, RegValue> >& regs,
                   const vector<pair<RegAddr, string> >& loads,
                   const vector<string>& extradevs,
                   bool quiet, bool doload)
    : m_kernel(m_symtable, m_breakpoints),
      m_clock(m_kernel.CreateClock(config.getValue<unsigned long>("CoreFreq"))),
      m_root("", m_clock),
      m_breakpoints(m_kernel),
      m_config(config),
      m_bootrom(NULL)
{

    if (!quiet)
    {
        clog << endl 
             << "Instanciating components..." << endl;
    }

    PSize numProcessors = m_config.getValue<PSize>("NumProcessors");

    const size_t numProcessorsPerFPU = config.getValue<size_t>("NumProcessorsPerFPU");
    const PSize  numFPUs             = (numProcessors + numProcessorsPerFPU - 1) / numProcessorsPerFPU;

    string memory_type = config.getValue<string>("MemoryType");
    transform(memory_type.begin(), memory_type.end(), memory_type.begin(), ::toupper);

    Clock& memclock = m_kernel.CreateClock(config.getValue<size_t>("MemoryFreq"));

    if (memory_type == "SERIAL") {
        SerialMemory* memory = new SerialMemory("memory", m_root, memclock, config);
        m_memory = memory;
    } else if (memory_type == "PARALLEL") {
        ParallelMemory* memory = new ParallelMemory("memory", m_root, memclock, config);
        m_memory = memory;
    } else if (memory_type == "BANKED") {
        BankedMemory* memory = new BankedMemory("memory", m_root, memclock, config, "DIRECT");
        m_memory = memory;
    } else if (memory_type == "RANDOMBANKED") {
        BankedMemory* memory = new BankedMemory("memory", m_root, memclock, config, "RMIX");
        m_memory = memory;
    } else if (memory_type == "DDR") {
        DDRMemory* memory = new DDRMemory("memory", m_root, memclock, config, "DIRECT");
        m_memory = memory;
    } else if (memory_type == "RANDOMDDR") {
        DDRMemory* memory = new DDRMemory("memory", m_root, memclock, config, "RMIX");
        m_memory = memory;
    } else if (memory_type == "COMA") {
        COMA* memory = new TwoLevelCOMA("memory", m_root, memclock, config);
        m_memory = memory;
    } else if (memory_type == "ZLCOMA") {
        ZLCOMA* memory = new ZLCOMA("memory", m_root, memclock, config);
        m_memory = memory;
    } else if (memory_type == "FLATCOMA") {
        COMA* memory = new OneLevelCOMA("memory", m_root, memclock, config);
        m_memory = memory;
    } else {
        throw runtime_error("Unknown memory type: " + memory_type);
    }
    if (!quiet)
    {
        clog << "memory: " << memory_type << endl;
    }

    // Create the event selector
    Clock& selclock = m_kernel.CreateClock(config.getValue<unsigned long>("EventCheckFreq"));
    m_selector = new Selector("selector", m_root, selclock, config);

    // Create the I/O Buses
    const size_t numIOBuses = config.getValue<size_t>("NumIOBuses");
    m_iobuses.resize(numIOBuses);
    for (size_t b = 0; b < numIOBuses; ++b)
    {
        stringstream ss;
        ss << "iobus" << b;
        string name = ss.str();

        string bus_type = config.getValue<string>(m_root, name, "Type");
        Clock& ioclock = m_kernel.CreateClock(config.getValue<unsigned long>(m_root, name, "Freq"));

        if (bus_type == "NULLIO") {
            NullIO* bus = new NullIO(name, m_root, ioclock);
            m_iobuses[b] = bus;
            config.registerObject(*bus, "nullio");
            config.registerProperty(*bus, "freq", (uint32_t)ioclock.GetFrequency());
        } else {
            throw runtime_error("Unknown I/O bus type for " + name + ": " + bus_type);
        }

        if (!quiet)
        {
            clog << name << ": " << bus_type << endl;
        }
    }

    // Create the FPUs
    m_fpus.resize(numFPUs);
    for (size_t f = 0; f < numFPUs; ++f)
    {
        stringstream name;
        name << "fpu" << f;
        m_fpus[f] = new FPU(name.str(), m_root, m_clock, config, numProcessorsPerFPU);

        config.registerObject(*m_fpus[f], "fpu");
        config.registerProperty(*m_fpus[f], "freq", (uint32_t)m_clock.GetFrequency());
    }
    if (!quiet)
    {
        clog << numFPUs << " FPUs instantiated." << endl;
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
        if (config.getValueOrDefault<bool>(m_root, name, "EnableIO", false)) // I/O disabled unless specified
        {
            size_t busid = config.getValue<size_t>(m_root, name, "BusID");
            if (busid >= m_iobuses.size())
            {
                throw runtime_error("Processor " + name + " set to connect to non-existent bus");
            }

            m_procbusmapping[i] = busid;
            iobus = m_iobuses[busid];

            if (!quiet)
            {
                clog << name << ": connected to " << dynamic_cast<Object*>(iobus)->GetName() << endl;
            }
        }

        m_procs[i]   = new Processor(name, m_root, m_clock, i, m_procs, *m_memory, *m_memory, fpu, iobus, config);
    }
    if (!quiet)
    {
        clog << numProcessors << " cores instantiated." << endl;
    }
    
    // Create the I/O devices
    vector<string> dev_names = extradevs;
    vector<string> cfg_names = config.getWordList("IODevices");
    copy(cfg_names.begin(), cfg_names.end(), back_inserter(dev_names));
    size_t numIODevices = dev_names.size();

    m_devices.resize(numIODevices);
    vector<ActiveROM*> aroms;

    UnixInterface *uif = new UnixInterface("unix_if", m_root);
    m_devices.push_back(uif);

    for (size_t i = 0; i < numIODevices; ++i)
    {
        string name = dev_names[i];

        bool enable_dev = config.getValueOrDefault<bool>(m_root, name, "EnableDevice", true);
        if (!enable_dev)
            continue;

        size_t busid = config.getValue<size_t>(m_root, name, "BusID");

        if (busid >= m_iobuses.size())
        {
            throw runtime_error("Device " + name + " set to connect to non-existent bus");
        }
        
        IIOBus& iobus = *m_iobuses[busid];

        IODeviceID devid = config.getValueOrDefault<IODeviceID>(m_root, name, "DeviceID", iobus.GetNextAvailableDeviceID());

        string dev_type = config.getValue<string>(m_root, name, "Type");

        if (!quiet)
        {
            clog << name << ": connected to " << dynamic_cast<Object&>(iobus).GetName() << " (type " << dev_type << ", devid " << dec << devid << ')' << endl;
        }

        if (dev_type == "LCD") {
            LCD *lcd = new LCD(name, m_root, iobus, devid, config);
            m_devices[i] = lcd;
            config.registerObject(*lcd, "lcd");
        } else if (dev_type == "RTC") {
            Clock& rtcclock = m_kernel.CreateClock(config.getValue<size_t>(m_root, name, "RTCUpdateFreq"));
            RTC *rtc = new RTC(name, m_root, rtcclock, iobus, devid, config);
            m_devices[i] = rtc;
            config.registerObject(*rtc, "rtc");
        } else if (dev_type == "GFX") {
            size_t fbdevid = config.getValueOrDefault<size_t>(m_root, name, "GfxFrameBufferDeviceID", devid + 1);
            Display *disp = new Display(name, m_root, iobus, devid, fbdevid, config);
            m_devices[i] = disp;
            config.registerObject(*disp, "gfx");
        } else if (dev_type == "AROM") {
            ActiveROM *rom = new ActiveROM(name, m_root, *m_memory, iobus, devid, config, quiet);
            m_devices[i] = rom;
            aroms.push_back(rom);
            config.registerObject(*rom, "arom");
        } else if (dev_type == "UART") {
            UART *uart = new UART(name, m_root, iobus, devid, config);
            m_devices[i] = uart;
            config.registerObject(*uart, "uart");
        } else if (dev_type == "SMC") {
            SMC * smc = new SMC(name, m_root, iobus, devid, regs, loads, config);
            m_devices[i] = smc;
            config.registerObject(*smc, "smc");
        } else if (dev_type == "RPC") {
            RPCInterface* rpc = new RPCInterface(name, m_root, iobus, devid, config, *uif);
            m_devices[i] = rpc;
            config.registerObject(*rpc, "rpc");
        } else {
            throw runtime_error("Unknown I/O device type: " + dev_type);
        }

        config.registerBidiRelation(iobus, *m_devices[i], "client", (uint32_t)devid);
    }



    // We need to register the master frequency into the
    // configuration, because both in-program and external monitoring
    // want to know it.
    unsigned long masterfreq = m_kernel.GetMasterFrequency();
    config.getValueOrDefault("MasterFreq", masterfreq); // The lookup will set the config key as side effect

    config.registerObject(m_root, "system");
    config.registerProperty(m_root, "version", PACKAGE_VERSION);
    config.registerProperty(m_root, "masterfreq", (uint32_t)masterfreq);

    if (!quiet)
    {
        clog << endl 
             << "Initializing components..." << endl;
    }

    // Initialize the memory
    m_memory->Initialize();

    // Connect processors in the link
    for (size_t i = 0; i < numProcessors; ++i)
    {
        Processor* prev = (i == 0)                 ? NULL : m_procs[i - 1];
        Processor* next = (i == numProcessors - 1) ? NULL : m_procs[i + 1];
        m_procs[i]->Initialize(prev, next);
        if (next)
            config.registerRelation(*m_procs[i], *next, "link", true);
    }

    // Initialize the buses. This initializes the devices as well.
    for (size_t i = 0; i < m_iobuses.size(); ++i)
    {
        m_iobuses[i]->Initialize();
    }

    // Check for bootable ROMs. This must happen after I/O bus
    // initialization because the ROM contents are loaded then.
    for (size_t i = 0; i < aroms.size(); ++i)
    {
        ActiveROM *rom = aroms[i];
        if (rom->IsBootable())
        {
            if (m_bootrom != NULL)
            {
                throw runtime_error("More than one bootable ROM detected: " + rom->GetName() + ", " + m_bootrom->GetFQN());
            }
            m_bootrom = rom;
        }
    }
    if (m_bootrom == NULL)
    {
        cerr << "Warning: No bootable ROM configured." << endl;
    }

    if (!quiet)
    {
        clog << endl 
             << "Final configuration..." << endl;
    }

    // Set up the initial memory ranges
    size_t numRanges = config.getValue<size_t>("NumMemoryRanges");
    for (size_t i = 0; i < numRanges; ++i)
    {
        stringstream ss;
        ss << "MemoryRange" << i;
        string name = ss.str();

        MemAddr address = config.getValue<MemAddr>(name, "Address");
        MemSize size = config.getValue<MemSize>(name, "Size");
        string mode = config.getValue<string>(name, "Mode");
        ProcessID pid = config.getValue<ProcessID>(name, "PID");
        int perm = 0;
        if (mode.find("R") != string::npos)
            perm |= IMemory::PERM_READ;
        if (mode.find("W") != string::npos)
            perm |= IMemory::PERM_WRITE;
        if (mode.find("X") != string::npos)
            perm |= IMemory::PERM_EXECUTE;

        m_memory->Reserve(address, size, pid, perm);
    }

    // Set program debugging per default
    m_kernel.SetDebugMode(Kernel::DEBUG_PROG);

    // Load symbol table
    if (doload && !symtable.empty())
    {
        ifstream in(symtable.c_str(), ios::in);
        m_symtable.Read(in, quiet);
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
        unsigned int q;
        for (q = 0; masterfreq % 1000 == 0 && q < sizeof(qual)/sizeof(qual[0]); ++q)
        {
            masterfreq /= 1000;
        }
        clog << "Created Microgrid; simulation running at " << dec << masterfreq << " " << qual[q] << "Hz" << endl;
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
    delete m_selector;
    delete m_memory;
}
