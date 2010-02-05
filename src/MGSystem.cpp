#include "MGSystem.h"

#ifdef ENABLE_COMA
# include "CMLink.h"
# include "coma/simlink/th.h"
# include "coma/simlink/linkmgs.h"
#else
# include "SerialMemory.h"
# include "ParallelMemory.h"
# include "BankedMemory.h"
# include "RandomBankedMemory.h"
#endif

#include "loader.h"

// For Tokenize:
#include "simreadline.h"

#include <iomanip>
#include <iostream>
#include <cmath>

using namespace Simulator;
using namespace std;
#ifdef ENABLE_COMA
using namespace MemSim;
#endif

MemAddr MGSystem::WriteConfiguration(const Config& config)
    {
        const vector<PSize>& placeSizes = config.getIntegerList<PSize>("NumProcessors");
        
        vector<uint32_t> data(1 + m_procs.size());

        // Store the number of cores
        SerializeRegister(RT_INTEGER, m_procs.size(), &data[0], sizeof data[0]);

        // Store the cores, per place
        PSize first = 0;
        for (size_t p = 0; p < placeSizes.size(); ++p)
        {            
            PSize placeSize = placeSizes[p];
            for (size_t i = 0; i < placeSize; ++i)
            {
                PSize pid = first + i;
                SerializeRegister(RT_INTEGER, (p << 16) | (pid << 0), &data[1 + pid], sizeof data[0]);
            }
            first += placeSize;
        }
        
        MemAddr base;
        if (!m_memory->Allocate(data.size() * sizeof data[0], IMemory::PERM_READ, base))
        {
            throw runtime_error("Unable to allocate memory to store configuration data");
        }
        m_memory->Write(base, &data[0], data.size() * sizeof data[0]);
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
#ifdef ENABLE_COMA
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
           /* const Kernel::ComponentList& components = m_kernel.GetComponents();
            for (Kernel::ComponentList::const_iterator p = components.begin(); p != components.end(); ++p)
            {
                for (size_t i = 0; i < p->processes.size(); ++i)
                {
                    switch (p->processes[i].state)
                    {
                    case STATE_DEADLOCK: ++num_stalled; break;
                    case STATE_RUNNING:  ++num_running; break;
                    case STATE_ABORTED:  assert(0); break;
                    }
                }
            }*/
            
            unsigned int num_regs = 0;
            for (size_t i = 0; i < m_procs.size(); ++i)
            {
                num_regs += m_procs[i]->GetNumSuspendedRegisters();
            }
            
            stringstream ss;
            ss << "Deadlock!" << endl
               << "(" << num_stalled << " processes stalled;  " << num_running << " processes running; "
               << num_regs << " registers waited on)";
            throw runtime_error(ss.str());
        }
                                
        if (state == STATE_ABORTED)
        {
            // The simulation was aborted, because the user interrupted it.
            throw runtime_error("Interrupted!");
        }
    }
    
MGSystem::MGSystem(const Config& config, Display& display, const string& program,
             const vector<pair<RegAddr, RegValue> >& regs,
             const vector<pair<RegAddr, string> >& loads,
             bool quiet)
        : Object("system", m_kernel),
          m_kernel(display)
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
        
#ifdef ENABLE_COMA
        m_objects.resize(numProcessors * 2 + numFPUs);
        CMLink** &m_pmemory = (CMLink**&)this->m_pmemory;
        m_pmemory = new CMLink*[LinkMGS::s_oLinkConfig.m_nProcs];
#else
        string memory_type = config.getString("MemoryType", "");
        std::transform(memory_type.begin(), memory_type.end(), memory_type.begin(), ::toupper);
        
        m_objects.resize(numProcessors + numFPUs + 1);
        if (memory_type == "SERIAL") {
            SerialMemory* memory = new SerialMemory("memory", *this, config);
            m_objects.back() = memory;
            m_memory = memory;
        } else if (memory_type == "PARALLEL") {
            ParallelMemory* memory = new ParallelMemory("memory", *this, config);
            m_objects.back() = memory;
            m_memory = memory;
        } else if (memory_type == "BANKED") {
            BankedMemory* memory = new BankedMemory("memory", *this, config);
            m_objects.back() = memory;
            m_memory = memory;
        } else if (memory_type == "RANDOMBANKED") {
            RandomBankedMemory* memory = new RandomBankedMemory("memory", *this, config);            
            m_objects.back() = memory;
            m_memory = memory;
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
#ifdef ENABLE_COMA
                stringstream namem;
                namem << "memory" << pid;
                m_pmemory[pid] = new CMLink(namem.str(), *this, config, g_pLinks[i]);
                if (pid == 0)
                    m_memory = m_pmemory[0];
                m_procs[pid]   = new Processor(name.str(), *this, pid, i, m_procs, m_procs.size(), *m_places[p], *m_pmemory[pid], display, fpu, config);  
                m_pmemory[pid]->SetProcessor(m_procs[pid]);
                m_objects[pid+numProcessors] = m_pmemory[pid];
#else
                m_procs[pid]   = new Processor(name.str(), *this, pid, i, m_procs, m_procs.size(), *m_places[p], *m_memory, display, fpu, config);
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
                value.m_integer = LoadDataFile(m_memory, loads[i].second, quiet);
                m_procs[0]->WriteRegister(loads[i].first, value); 
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


