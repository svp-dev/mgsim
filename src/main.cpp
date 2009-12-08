#ifdef HAVE_CONFIG_H
#include "sys_config.h"
#endif

#include "simreadline.h"

#include "Processor.h"
#include "FPU.h"
#ifdef ENABLE_COMA
# include "CMLink.h"
# include "coma/simlink/th.h"
# include "coma/simlink/linkmgs.h"
# include "memdump.h"

const char* semaphore_journal = "/tmp/simx-sem-journal";
#else
# include "SerialMemory.h"
# include "ParallelMemory.h"
# include "BankedMemory.h"
# include "RandomBankedMemory.h"
#endif
#include "display.h"
#include "memstrace.h"

#include "config.h"
#include "loader.h"

#include <cassert>
#include <iostream>
#include <iomanip>
#include <limits>
#include <typeinfo>
#include <cmath>
#include <algorithm>

#include <sys/time.h>
#include <signal.h>
#include <cstdlib>
#include <cxxabi.h>

#ifdef USE_SDL
#include <SDL.h>
#endif

using namespace Simulator;
using namespace std;
#ifdef ENABLE_COMA
using namespace MemSim;

static bool dump(const char* pstr, bool bforce)
{
    ofstream dumpfile(pstr, ofstream::binary);
    if (dumpfile.fail())
    {
        cerr << "dump: cannot open dump file " << pstr << endl;
        dumpfile.close();
        return false;
    }

    dumpcacheandmemory(dumpfile, bforce);

    dumpfile.close();
    return true;
}
#endif


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

static string Trim(const string& str)
{
    size_t beg = str.find_first_not_of(" \t");
    if (beg != string::npos)
    {
        size_t end = str.find_last_not_of(" \t");
        return str.substr(beg, end - beg + 1);
    }
    return "";
}

class MGSystem : public Object
{
    vector<Processor*> m_procs;
    vector<FPU*>       m_fpus;
    vector<Object*>    m_objects;
    vector<PlaceInfo*> m_places;
    Kernel             m_kernel;
    IMemoryAdmin*      m_memory;
#ifdef ENABLE_COMA
    CMLink**           m_pmemory;
#endif
    // Writes the current configuration into memory and returns its address
    MemAddr WriteConfiguration(const Config& config)
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

public:
    // Get or set the debug flag
    int  GetDebugMode() const   { return m_kernel.GetDebugMode(); }
    void SetDebugMode(int mode) { m_kernel.SetDebugMode(mode); }
    void ToggleDebugMode(int mode) { m_kernel.ToggleDebugMode(mode); }

    uint64_t GetOp() const
    {
        uint64_t op = 0;
        for (size_t i = 0; i < m_procs.size(); ++i) {
            op += m_procs[i]->GetOp();
        }
        return op;
    }

    uint64_t GetFlop() const
    {
        uint64_t flop = 0;
        for (size_t i = 0; i < m_procs.size(); ++i) {
            flop += m_procs[i]->GetFlop();
        }
        return flop;
    }

    void PrintState(const vector<string>& arguments) const
    {
        typedef map<string, RunState> StateMap;
		
        bool show_all = (!arguments.empty() && arguments[0] == "all");
		
        StateMap   states;
        streamsize length = 0;

        const Kernel::ComponentList& components = m_kernel.GetComponents();
        for (Kernel::ComponentList::const_iterator p = components.begin(); p != components.end(); ++p)
        {
            for (size_t i = 0; i < p->processes.size(); ++i)
            {
                RunState state = p->processes[i].state;
                if (show_all || state != STATE_IDLE) {
                    const string name = p->component->GetFQN() + ":" + p->processes[i].name + ": ";
                    states[name] = state;
                    length = max(length, (streamsize)name.length());
                }
    	    }
        }
		
        cout << left << setfill(' ');
        for (StateMap::const_iterator p = states.begin(); p != states.end(); ++p)
        {
            cout << setw(length) << p->first;
            switch (p->second)
            {
            case STATE_IDLE:     cout << "idle";    break;
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
            bool idle = m_procs[i]->IsIdle();
            if (show_all || !idle) {
                cout << "Processor " << dec << right << setw(width) << i << ": "
                     << (idle ? "empty" : "non-empty") << endl;
            }
        }
    }

    void PrintRegFileAsyncPortActivity(std::ostream& os) const
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

    void PrintPipelineEfficiency(std::ostream& os) const
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
	
    void PrintPipelineIdleTime(std::ostream& os) const
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

    void PrintAllFamilyCompletions(std::ostream& os) const
    {
        for (PSize i = 0; i < m_procs.size(); i++) {
            CycleNo last = m_procs[i]->GetLocalFamilyCompletion();
            if (last != 0)
                os << m_procs[i]->GetLocalFamilyCompletion() 
                   << "\t# cycle counter at last family completion on core " << i
                   << endl;
        }
    }

    void PrintFamilyCompletions(std::ostream& os) const
    {
        CycleNo first = UINT64_MAX;
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

    const Kernel& GetKernel() const { return m_kernel; }
    Kernel& GetKernel()       { return m_kernel; }

    // Find a component in the system given its path
    // Returns NULL when the component is not found
    Object* GetComponent(const string& path)
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
    void Step(CycleNo nCycles)
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
            unsigned int num_idle = 0, num_stalled = 0, num_running = 0;
            const Kernel::ComponentList& components = m_kernel.GetComponents();
            for (Kernel::ComponentList::const_iterator p = components.begin(); p != components.end(); ++p)
            {
                for (size_t i = 0; i < p->processes.size(); ++i)
                {
                    switch (p->processes[i].state)
                    {
                    case STATE_IDLE:     ++num_idle;    break;
                    case STATE_DEADLOCK: ++num_stalled; break;
                    case STATE_RUNNING:  ++num_running; break;
                    case STATE_ABORTED:  assert(0); break;
                    }
                }
            }
            
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
    
    void Abort()
    {
        GetKernel().Abort();
    }
    
    MGSystem(const Config& config, Display& display, const string& program,
             const vector<pair<RegAddr, RegValue> >& regs,
             const vector<pair<RegAddr, string> >& loads,
             bool quiet)
        : Object(NULL, NULL, "system"),
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
        m_pmemory = new CMLink*[LinkMGS::s_oLinkConfig.m_nProcLink];
#else
        string memory_type = config.getString("MemoryType", "");
        std::transform(memory_type.begin(), memory_type.end(), memory_type.begin(), ::toupper);
        
        m_objects.resize(numProcessors + numFPUs + 1);
        if (memory_type == "SERIAL") {
            SerialMemory* memory = new SerialMemory(this, m_kernel, "memory", config);
            m_objects.back() = memory;
            m_memory = memory;
        } else if (memory_type == "PARALLEL") {
            ParallelMemory* memory = new ParallelMemory(this, m_kernel, "memory", config);
            m_objects.back() = memory;
            m_memory = memory;
        } else if (memory_type == "BANKED") {
            BankedMemory* memory = new BankedMemory(this, m_kernel, "memory", config);
            m_objects.back() = memory;
            m_memory = memory;
        } else if (memory_type == "RANDOMBANKED") {
            RandomBankedMemory* memory = new RandomBankedMemory(this, m_kernel, "memory", config);            
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
            m_fpus[f] = new FPU(this, m_kernel, name.str(), config, numProcessorsPerFPU);
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
                if (pid >= LinkMGS::s_oLinkConfig.m_nProcLink)
                { std::cerr << "Too many memory links!" << std::endl; exit(1); }
                m_pmemory[pid] = new CMLink(this, m_kernel, namem.str(), config, g_pLinks[i], g_pMemoryDataContainer);
                if (pid == 0)
                    m_memory = m_pmemory[0];
                m_procs[pid]   = new Processor(this, m_kernel, pid, i, m_procs, m_procs.size(), *m_places[p], name.str(), *m_pmemory[pid], display, fpu, config);  
                m_pmemory[pid]->SetProcessor(m_procs[pid]);
                m_objects[pid+numProcessors] = m_pmemory[pid];
#else
                m_procs[pid]   = new Processor(this, m_kernel, pid, i, m_procs, m_procs.size(), *m_places[p], name.str(), *m_memory, display, fpu, config);
#endif
                m_objects[pid] = m_procs[pid];
            }
            first += m_places[p]->m_size;
        }

        // Load the program into memory
        std::pair<MemAddr, bool> progdesc = LoadProgram(m_memory, program, quiet);
        
        m_kernel.Initialize();

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

    ~MGSystem()
    {
        delete m_memory;
        for (size_t i = 0; i < m_procs.size(); ++i)
        {
            delete m_procs[i];
        }
        for (size_t i = 0; i < m_places.size(); ++i)
        {
            delete m_places[i];
        }
    }
};

class CommandLineReader {
    string   m_histfilename;
    static Display* m_display;

    static int ReadLineHook(void) {
#ifdef CHECK_DISPLAY_EVENTS
        if (!m_display) 
            return 0;

        // readline is annoying: the documentation says the event
        // hook is called no more than 10 times per second, but
        // tests show it _can_ be much more often than this. But
        // we don't want to refresh the display or check events
        // too often (it's expensive...) So we keep track of time
        // here as well, and force 10ths of seconds ourselves.
        
        static long last_check = 0;
        
        struct timeval tv;
        gettimeofday(&tv, 0);
        long current = tv.tv_sec * 10 + tv.tv_usec / 100000;
        if (current - last_check)
        {
            m_display->CheckEvents();
            last_check = current;
        }
#endif
        return 0;
    }

public:
    CommandLineReader(Display& d)  {
        m_display = &d;
        rl_event_hook = &ReadLineHook;
#ifdef HAVE_READLINE_HISTORY
        std::ostringstream os;
        os << getenv("HOME") << "/.mgsim_history";
        m_histfilename = os.str();
        read_history(m_histfilename.c_str());
#endif
    }

    ~CommandLineReader() {
        rl_event_hook = 0;
        m_display = 0;
        CheckPointHistory();
    }

    char* GetCommandLine(const string& prompt)
    {
        char* str = readline(prompt.c_str());
#ifdef HAVE_READLINE_HISTORY
        if (str != NULL && *str != '\0')
        {
            add_history(str);
        }
#endif
        return str;
    }

    void CheckPointHistory() {
#ifdef HAVE_READLINE_HISTORY
        write_history(m_histfilename.c_str());
#endif
    }
};

Display* CommandLineReader::m_display = 0;

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

// Print all components that are a child of root
static void PrintComponents(const Object* root, const string& indent = "")
{
    for (unsigned int i = 0; i < root->GetNumChildren(); ++i)
    {
        const Object* child = root->GetChild(i);
        string str = indent + child->GetName();

        cout << str << " ";
        for (size_t len = str.length(); len < 30; ++len) cout << " ";
        cout << GetClassName(typeid(*child)) << endl;

        PrintComponents(child, indent + "  ");
    }
}

// Prints the help text
static void PrintHelp(ostream& out)
{
    out <<
        "Available commands:\n"
        "-------------------\n"
        "(h)elp           Print this help text.\n"
        "(p)rint          Print all components in the system.\n"
        "(s)tep           Advance the system one clock cycle.\n"
        "(r)un            Run the system until it is idle or deadlocks.\n"
        "                 Livelocks will not be reported.\n"
        "state [all]      Shows the state of the system. When \"all\" is\n"
        "                 not specified, it leaves out all idle components\n"
        "debug [mode]     Show debug mode or set debug mode\n"
        "                 Debug mode can be: SIM, PROG, DEADLOCK or NONE.\n"
        "                 ALL is short for SIM and PROG\n"
        "help <component> Show the supported methods and options for this\n"
        "                 component.\n"
        << endl;
}

class bind_cmd
{
public:
    virtual bool call(std::ostream& out, Object* obj, const std::vector<std::string>& arguments) const = 0;
    virtual ~bind_cmd() {}
};

template <typename T>
class bind_cmd_T : public bind_cmd
{
    typedef void (T::*func_t)(std::ostream& out, const std::vector<std::string>& arguments) const;
    func_t m_func;
public:
    bool call(std::ostream& out, Object* obj, const std::vector<std::string>& arguments) const {
        T* o = dynamic_cast<T*>(obj);
        if (o == NULL) return false;
        (o->*m_func)(out, arguments);
        return true;
    }
    bind_cmd_T(const func_t& func) : m_func(func) {}
};

static const struct
{
    const char*     name;
    const bind_cmd* func;
} _Commands[] = {
    {"help", new bind_cmd_T<RAUnit            >(&RAUnit            ::Cmd_Help) },
    {"help", new bind_cmd_T<ThreadTable       >(&ThreadTable       ::Cmd_Help) },
    {"help", new bind_cmd_T<FamilyTable       >(&FamilyTable       ::Cmd_Help) },
    {"help", new bind_cmd_T<Network           >(&Network           ::Cmd_Help) },
    {"help", new bind_cmd_T<RegisterFile      >(&RegisterFile      ::Cmd_Help) },
    {"help", new bind_cmd_T<ICache            >(&ICache            ::Cmd_Help) },
    {"help", new bind_cmd_T<DCache            >(&DCache            ::Cmd_Help) },
    {"help", new bind_cmd_T<Pipeline          >(&Pipeline          ::Cmd_Help) },
    {"help", new bind_cmd_T<Allocator         >(&Allocator         ::Cmd_Help) },
#ifndef ENABLE_COMA
    {"help", new bind_cmd_T<SerialMemory      >(&SerialMemory      ::Cmd_Help) },
    {"help", new bind_cmd_T<ParallelMemory    >(&ParallelMemory    ::Cmd_Help) },
    {"help", new bind_cmd_T<RandomBankedMemory>(&RandomBankedMemory::Cmd_Help) },
    {"help", new bind_cmd_T<BankedMemory      >(&BankedMemory      ::Cmd_Help) },
#endif
    {"help", new bind_cmd_T<FPU               >(&FPU               ::Cmd_Help) },
    {"info", new bind_cmd_T<VirtualMemory     >(&VirtualMemory     ::Cmd_Info) },
    {"read", new bind_cmd_T<RAUnit            >(&RAUnit            ::Cmd_Read) },
    {"read", new bind_cmd_T<ThreadTable       >(&ThreadTable       ::Cmd_Read) },
    {"read", new bind_cmd_T<FamilyTable       >(&FamilyTable       ::Cmd_Read) },
    {"read", new bind_cmd_T<Network           >(&Network           ::Cmd_Read) },
    {"read", new bind_cmd_T<RegisterFile      >(&RegisterFile      ::Cmd_Read) },
    {"read", new bind_cmd_T<ICache            >(&ICache            ::Cmd_Read) },
    {"read", new bind_cmd_T<DCache            >(&DCache            ::Cmd_Read) },
    {"read", new bind_cmd_T<Pipeline          >(&Pipeline          ::Cmd_Read) },
    {"read", new bind_cmd_T<Allocator         >(&Allocator         ::Cmd_Read) },
#ifndef ENABLE_COMA
    {"read", new bind_cmd_T<SerialMemory      >(&SerialMemory      ::Cmd_Read) },
    {"read", new bind_cmd_T<ParallelMemory    >(&ParallelMemory    ::Cmd_Read) },
    {"read", new bind_cmd_T<RandomBankedMemory>(&RandomBankedMemory::Cmd_Read) },
    {"read", new bind_cmd_T<BankedMemory      >(&BankedMemory      ::Cmd_Read) },
#endif
    {"read", new bind_cmd_T<VirtualMemory     >(&VirtualMemory     ::Cmd_Read) },
    {"read", new bind_cmd_T<FPU               >(&FPU               ::Cmd_Read) },
    {NULL, NULL}
};

static void ExecuteCommand(MGSystem& sys, const string& command, vector<string> args)
{
    // Backup stream state before command
    stringstream backup;
    backup.copyfmt(cout);
    
    // See if the command exists
    int i;
    for (i = 0; _Commands[i].name != NULL; ++i)
    {
        if (_Commands[i].name == command)
        {
            if (args.size() == 0)
            {
                cout << "Please specify a component. Use \"print\" for a list of all components" << endl;
                break;
            }

            // Pop component name
            Object* obj = sys.GetComponent(args.front());
            args.erase(args.begin());

            if (obj == NULL)
            {
                cout << "Invalid component name" << endl;
            }
            else
            {
                // See if the object type matches
                int j;
                for (j = i; _Commands[j].name != NULL && _Commands[j].name == command; ++j)
                {
                    if (_Commands[j].func->call(cout, obj, args))
                    {
                        cout << endl;
                        break;
                    }
                }

                if (_Commands[j].name == NULL || _Commands[j].name != command)
                {
                    cout << "Invalid argument type for command" << endl;
                }
            }
            break;
        }
    }

    if (_Commands[i].name == NULL)
    {
        // Command does not exist
        cout << "Unknown command" << endl;
    }
    
    // Restore stream state
    cout.copyfmt(backup);
}

static void PrintUsage()
{
    cout <<
        "MGSim [options] <program-file>\n\n"
        "Options:\n"
        "-h, --help               Show this help\n"
        "-c, --config <filename>  Read configuration from file\n"
        "-q, --quiet              Do not print simulation statistics after run\n" 
        "-i, --interactive        Start the simulator in interactive mode\n"
        "-t, --terminate          Terminate simulator on exception\n"
        "-R<X> <value>            Store the integer value in the specified register\n"
        "-F<X> <value>            Store the FP value in the specified register\n"
        "-L<X> <filename>         Load the contents of the file after the program\n" 
        "                         and store the address in the specified register\n" 
        "-o, --override <n>=<v>   Overrides the configuration option <n> with value <v>\n"
        "-d, --dumpconf           Dump configuration to standard error prior to program startup\n"
#ifdef ENABLE_COMA
        "--ddr <file>             Read DDR channel configurations from file\n"
        "--verbose <n>            Set memory verbosity\n" 
        "--memlog <file>          Output memory log to file\n"
#endif
        "\n";
}

struct ProgramConfig
{
#ifdef ENABLE_COMA
    unsigned           m_ncache;
    unsigned           m_ndirectory;
#endif
    string             m_programFile;
    string             m_configFile;
    bool               m_interactive;
    bool               m_terminate;
    bool               m_dumpconf;
    bool               m_quiet;
    map<string,string> m_overrides;
	
    vector<pair<RegAddr, RegValue> > m_regs;
    vector<pair<RegAddr, string> >   m_loads;
};

static void ParseArguments(int argc, const char ** argv, ProgramConfig& config
#ifdef ENABLE_COMA
			   , LinkConfig& lkconfig
#endif
    )
{
#ifdef ENABLE_COMA
    config.m_ncache = 0;
    config.m_ndirectory = 0xffff;
#endif
    config.m_configFile = MGSIM_CONFIG_PATH;
    config.m_interactive = false;
    config.m_terminate = false;
    config.m_dumpconf = false;
    config.m_quiet = false;

    for (int i = 1; i < argc; ++i)
    {
        const string arg = argv[i];
        if (arg[0] != '-')
        {
            if (config.m_programFile != "")
                cerr << "Warning: program already set ("
                     << config.m_programFile
                     << "), ignoring extra arg: " << arg << endl;
            else 
                config.m_programFile = arg;
        }
        else if (arg == "-c" || arg == "--config")      config.m_configFile  = argv[++i];
        else if (arg == "-i" || arg == "--interactive") config.m_interactive = true;
        else if (arg == "-t" || arg == "--terminate")   config.m_terminate   = true;
        else if (arg == "-q" || arg == "--quiet")       config.m_quiet       = true;
        else if (arg == "-h" || arg == "--help")        { PrintUsage(); exit(0); }
        else if (arg == "-d" || arg == "--dumpconf")    config.m_dumpconf    = true;
#ifdef ENABLE_COMA
        else if (arg == "--ddr")        lkconfig.m_sDDRXML = argv[++i];
        else if (arg == "--verbose")        			{lkconfig.m_nDefaultVerbose = atoi(argv[++i]);}
        else if (arg == "--memlog")						{lkconfig.m_pGlobalLogFile = (char*)argv[++i];}
#endif
        else if (arg == "-o" || arg == "--override")
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected configuration option");
            }
            string arg = argv[i];
            string::size_type eq = arg.find_first_of("=");
            if (eq == string::npos) {
                throw runtime_error("Error: malformed configuration override syntax");
            }
            string name = arg.substr(0, eq);
            transform(name.begin(), name.end(), name.begin(), ::toupper);
            config.m_overrides[name] = arg.substr(eq + 1);
        }
        else if (toupper(arg[1]) == 'L')  
        { 
            string filename(argv[++i]); 
            char* endptr; 
            RegAddr  addr; 
            unsigned long index = strtoul(&arg[2], &endptr, 0); 
            if (*endptr != '\0') { 
                throw runtime_error("Error: invalid register specifier in option"); 
            } 
            addr = MAKE_REGADDR(RT_INTEGER, index);                      
            config.m_loads.push_back(make_pair(addr, filename)); 
        } 
        else if (toupper(arg[1]) == 'R' || toupper(arg[1]) == 'F')
        {
            stringstream value;
            value << argv[++i];

            RegAddr  addr;
            RegValue val;

            char* endptr;
            unsigned long index = strtoul(&arg[2], &endptr, 0);
            if (*endptr != '\0') {
             	throw runtime_error("Error: invalid register specifier in option");
            }
                
            if (toupper(arg[1]) == 'R') {
                value >> *(signed Integer*)&val.m_integer;
                addr = MAKE_REGADDR(RT_INTEGER, index);
            } else {
                double f;
                value >> f;
                val.m_float.fromfloat(f);
                addr = MAKE_REGADDR(RT_FLOAT, index);
            }
            if (value.fail()) {
                throw runtime_error("Error: invalid value for register");
            }
            val.m_state = RST_FULL;
            config.m_regs.push_back(make_pair(addr, val));
        }
    }

    if (config.m_programFile.empty())
    {
        throw runtime_error("Error: no program file specified");
    }

}

/// The currently active system, for the signal handler
static MGSystem* active_system = NULL;

static void sigabrt_handler(int)
{
    if (active_system != NULL)
    {
        active_system->Abort();
        active_system = NULL;
    }
}

static void StepSystem(MGSystem& system, CycleNo cycles)
{
    active_system = &system;

    struct sigaction new_handler, old_handler;
    new_handler.sa_handler = sigabrt_handler;
    new_handler.sa_flags   = 0;
    sigemptyset(&new_handler.sa_mask);
    sigaction(SIGINT, &new_handler, &old_handler);

    try
    {
        system.Step(cycles);
    }
    catch (...)
    {
        sigaction(SIGINT, &old_handler, NULL);
        active_system = NULL;
        throw;
    }
    sigaction(SIGINT, &old_handler, NULL);   
    active_system = NULL;
}

static void PrintException(ostream& out, const exception& e)
{
    out << endl << e.what() << endl;
    
    const SimulationException* se = dynamic_cast<const SimulationException*>(&e);
    if (se != NULL)
    {
    	// SimulationExceptions hold more information, print it
    	const list<string>& details = se->GetDetails();
    	for (list<string>::const_iterator p = details.begin(); p != details.end(); ++p)
    	{
    	    out << *p << endl;
        }
    }
}

#ifdef ENABLE_COMA
void ConfigureCOMA(ProgramConfig& config, Config& configfile, LinkConfig& lkconfig) 
{
    PSize numProcessors = 0;

    // Get total number of cores:
    const vector<PSize> placeSizes = 
        configfile.getIntegerList<PSize>("NumProcessors");
    for (size_t i = 0; i < placeSizes.size(); ++i) 
        numProcessors += placeSizes[i];
    lkconfig.m_nProcMGS = numProcessors;

    // Get cache and directory configuration:
    size_t ncache = 
        configfile.getInteger<size_t>("NumCaches", 
                                      (numProcessors >= 4) ? (numProcessors / 4) : 1);
    size_t ndir = configfile.getInteger<size_t>("NumDirectories", ncache / 8);
    lkconfig.m_nLineSize = configfile.getInteger<size_t> ("CacheLineSize", 64);
  
    if (lkconfig.m_nProcLink < lkconfig.m_nProcMGS) {
        cerr << "warning: nProcLink (" 
             << lkconfig.m_nProcLink
             << ") < NumProcessors, adjusting nProcLink" 
             << endl;
        lkconfig.m_nProcLink = lkconfig.m_nProcMGS;
    }
    if (lkconfig.m_nProcLink < ncache) {
        cerr << "warning: NumCaches (" << ncache
             << ") > nProcLink (" 
             << lkconfig.m_nProcLink 
             << "), adjusting NumCaches" 
             << endl;
        ncache = lkconfig.m_nProcLink;
    }
    lkconfig.m_nCache = ncache;

    if (ncache < ndir) {
        cerr << "warning: NumDirectories (" 
             << ndir 
             << ") > NumCaches (" 
             << ncache 
             << "), adjusting NumDirectories" 
             << endl;
        ndir = ncache;
    }
    lkconfig.m_nDirectory = ndir;

    lkconfig.m_nSplitRootNumber = 
        configfile.getInteger<size_t>("NumSplitRootDirectories", 4); 
    lkconfig.m_nMemoryChannelNumber = 
        configfile.getInteger<size_t>("NumMemoryChannels", 
                                      lkconfig.m_nSplitRootNumber);

    // Check DDR configuration:
  
    lkconfig.m_nDDRConfigID = 
        configfile.getInteger<size_t>("DDRConfiguration", 0);
  
    lkconfig.m_nChannelInterleavingScheme = 
        configfile.getInteger<size_t>("ChannelInterleavingScheme", 0);

    // Cache properties:
  
    lkconfig.m_nCacheAccessTime = 
        configfile.getInteger<size_t>("L2CacheDelay", 2);
    lkconfig.m_nCacheAssociativity = 
        configfile.getInteger<size_t>("L2CacheAssociativity", 4);
    lkconfig.m_nCacheSet = 
        configfile.getInteger<size_t>("L2CacheNumSets", 128);

    lkconfig.m_nInject = configfile.getBoolean("EnableCacheInjection", true);

    size_t corefreq = configfile.getInteger<size_t>("CoreFreq", 1000);
    size_t memfreq = configfile.getInteger<size_t>("DDRMemoryFreq", 800);

    double ps_per_memcycle = (1./(memfreq*1e6))/1e-12;
    double ps_per_corecycle = (1./(corefreq*1e6))/1e-12;

    lkconfig.m_nCycleTimeCore = (size_t)ps_per_corecycle;
    lkconfig.m_nCycleTimeMemory = (size_t)ps_per_memcycle;

    // FIXME: maybe the following is not used anymore
    lkconfig.m_nMemorySize = DEFAULT_DUMP_SIZE;

    lkconfig.m_bConfigDone = true;

}
#endif


#ifdef ENABLE_COMA
int mgs_main(int argc, char const** argv)
#else    
# ifdef USE_SDL
    extern "C"
# endif
    int main(int argc, char** argv)
#endif
{
    try
    {
        // Parse command line arguments
        ProgramConfig config;
#ifdef ENABLE_COMA
        ParseArguments(argc, (const char**)argv, config, LinkMGS::s_oLinkConfig);
#else
        ParseArguments(argc, (const char**)argv, config);
#endif

        if (config.m_interactive)
        {
            // Interactive mode
            cout << "Microthreaded Alpha System Simulator, version 1.0" << endl;
            cout << "Created by Mike Lankamp at the University of Amsterdam" << endl << endl;
        }

        // Read configuration
        Config configfile(config.m_configFile, config.m_overrides);

        if (config.m_dumpconf)
            configfile.dumpConfiguration(std::clog, config.m_configFile);

#ifdef ENABLE_COMA
        ConfigureCOMA(config, configfile, LinkMGS::s_oLinkConfig);
        if (config.m_dumpconf)
            LinkMGS::s_oLinkConfig.dumpConfiguration(std::clog);
#endif

#ifdef ENABLE_COMA
        // finishing parsing config, now wait untile systemc topology is setup
        sem_post(&thpara.sem_sync);
        sem_wait(&thpara.sem_mgs);
        sem_post(&thpara.sem_sync);
#endif

        // Create the display
        Display display(configfile);

        // Create the system
        MGSystem sys(configfile, display, config.m_programFile, config.m_regs, config.m_loads, !config.m_interactive);

#if defined(ENABLE_COMA) && defined(MEM_DATA_PREFILL)
        sem_post(&thpara.sem_sync);
        sem_wait(&thpara.sem_mgs);
#endif

        bool interactive = config.m_interactive;
        if (!interactive)
        {
#ifdef ENABLE_COMA
            setverboselevel(LinkMGS::s_oLinkConfig.m_nDefaultVerbose);
#endif
            // Non-interactive mode; run and dump cycle count
            try
            {
                StepSystem(sys, INFINITE_CYCLES);
    			
                if (!config.m_quiet)
                {
                    clog << "### begin end-of-simulation statistics" << endl << dec;
                    clog << sys.GetKernel().GetCycleNo() << "\t# cycle counter at end of simulation" << endl
                         << sys.GetOp() << "\t# total executed instructions" << endl
                         << sys.GetFlop() << "\t# total issued fp instructions" << endl;
                    sys.PrintRegFileAsyncPortActivity(clog);
                    sys.PrintPipelineIdleTime(clog);
                    sys.PrintPipelineEfficiency(clog);
                    sys.PrintFamilyCompletions(clog);
                    sys.PrintAllFamilyCompletions(std::clog);
#ifdef ENABLE_COMA
                    clog << LinkMGS::s_oLinkConfig.m_nProcLink << "\t# COMA: nProcLink" << endl
			 << LinkMGS::s_oLinkConfig.m_nProcMGS << "\t# COMA: number of connected cores" << endl
			 << LinkMGS::s_oLinkConfig.m_nCache << "\t# COMA: number of L2 caches" << endl
			 << LinkMGS::s_oLinkConfig.m_nDirectory << "\t# COMA: number of first-level directories" << endl
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
                    clog << "### end end-of-simulation statistics" << endl;
                }
#ifdef ENABLE_COMA
                // stop the systemc and unlock the signal if it's locked
                thpara.bterm = true;
                sem_post(&thpara.sem_sync);
                sem_wait(&thpara.sem_mgs);
#endif

            }
            catch (const exception& e)
            {
                if (config.m_terminate) 
                {
                    // We do not want to go to interactive mode,
                    // rethrow so it abort the program.
                    throw;
                }
                
                PrintException(cerr, e);
    		    
                // When we get an exception in non-interactive mode,
                // jump into interactive mode
                interactive = true;
            }
        }
        
        if (interactive)
        {
            // Command loop
            vector<string> prevCommands;
            cout << endl;
            CommandLineReader clr(display);

            for (bool quit = false; !quit; )
            {
                stringstream prompt;
                prompt << dec << setw(8) << setfill('0') << right << sys.GetKernel().GetCycleNo() << "> ";
            
                // Read the command line and split into commands
                char* line = clr.GetCommandLine(prompt.str());
                if (line == NULL)
                {
                    // End of input
                    cout << endl;
                    break;
                }
				
                vector<string> commands = Tokenize(line, ";");
                if (commands.size() == 0)
                {
                    // Empty line, use previous command line
                    commands = prevCommands;
                }
                prevCommands = commands;
                free(line);

                // Execute all commands
                for (vector<string>::const_iterator command = commands.begin(); command != commands.end() && !quit; ++command)
                {
                    vector<string> args = Tokenize(Trim(*command), " ");
                    if (args.size() > 0)
                    {
                        // Pop the command from the front
                        string command = args[0];
                        args.erase(args.begin());

                        if (command == "h" || command == "/?" || (command == "help" && args.empty()))
                        {
                            PrintHelp(cout);
                        }
                        else if (command == "r" || command == "run" || command == "s" || command == "step")
                        {
                            // Step of run
                            CycleNo nCycles = INFINITE_CYCLES;
                            if (command[0] == 's')
                            {
                                // Step
                                char* endptr;
                                nCycles = args.empty() ? 1 : max(1UL, strtoul(args[0].c_str(), &endptr, 0));
                            }

                            // Flush the history, in case something
                            // bad happens in StepSystem...
                            clr.CheckPointHistory();

                            try
                            {
                                StepSystem(sys, nCycles);
                            }
                            catch (const exception& e)
                            {
                                PrintException(cerr, e);
                            }
                        }
                        else if (command == "p" || command == "print")
                        {
                            PrintComponents(&sys);
                        }
                        else if (command == "exit" || command == "quit")
                        {
                            cout << "Thank you. Come again!" << endl;
                            quit = true;
                            break;
                        }
                        else if (command == "state")
                        {
                            sys.PrintState(args);
                        }
                        else if (command == "debug")
                        {
                            string state;
                            if (!args.empty())
                            {
                                state = args[0];
                                transform(state.begin(), state.end(), state.begin(), ::toupper);
                            }
	        
                            if (state == "SIM")      sys.ToggleDebugMode(Kernel::DEBUG_SIM);
                            else if (state == "PROG")     sys.ToggleDebugMode(Kernel::DEBUG_PROG);
                            else if (state == "DEADLOCK") sys.ToggleDebugMode(Kernel::DEBUG_DEADLOCK);
                            else if (state == "ALL")      sys.SetDebugMode(-1);
                            else if (state == "NONE")     sys.SetDebugMode(0);
                            
                            string debugStr;
                            int m = sys.GetDebugMode();
			    if (m & Kernel::DEBUG_PROG)     debugStr += " program";
			    if (m & Kernel::DEBUG_SIM)      debugStr += " simulator";
			    if (m & Kernel::DEBUG_DEADLOCK) debugStr += " deadlocks";
			    if (!debugStr.size()) debugStr = " (nothing)";
			    cout << "Debugging:" << debugStr << endl;
                        }
#ifdef ENABLE_COMA
                        else if (command == "verbose")
                        {
                            if (args.empty())
                                setverboselevel(3);
                            else
                                setverboselevel(atoi(args[0].c_str()));
                        }
                        else if (command == "dump")
                        {
                            if (args.empty())
                            {
                                cout << "argument needed" << endl;
                            }
                            else if ((args.size() == 1) || (args[0] != "-f"))
                            {
                                dump(args[0].c_str(), false);
                            }
                            else
                            {
                                dump(args[1].c_str(), true);
                            }
                        }
                        else if (command == "checkmem")
                        {
                            if (!checkcacheandmemory())
                                cout << "checking memory failed. " << endl;
                        }
                        else if (command == "reviewmem")
                        {
                            reviewmemorysystem();
                        }
#ifdef MEM_MODULE_STATISTICS
                        else if (command == "stat")
                        {
                            if (args.empty())
                            {
                                cout << "argument needed" << endl;
                            }
                            else
                            {
                                printstatistics(args[0].c_str());
                            }

                        }
#endif
                        else if (command == "startmonitor")
                        {
                            if (!args.empty())
                            {
                                startmonitorfile(args[0].c_str());
                            }
                        }
                        else if (command == "stopmonitor")
                        {
                            stopmonitorfile();
                        }
                        else if (command == "monitor")
                        {
                            if (args.empty())
                            {
                            }
                            else
                            {
                                char *pend;
                                monitormemoryaddress((uint64_t)strtol(args[0].c_str(), &pend, 0));    // 2FIX_64
                            }
                        }
                        else if (command == "automonitor")
                        {
                            if (args.empty())
                            {}
                            else
                            {
                                char *pend;
                                automonitoraddress((uint64_t)strtol(args[0].c_str(), &pend, 0));    // 2FIX_64
                            }
                        }
                        else if (command == "traceaddr")
                        {
                            if (args.empty())
                            {}
                            else
                            {
                                char *pend;
                                settraceaddress((uint64_t)strtol(args[0].c_str(), &pend, 0));
                            }
                        }
                        else if (command == "tracep")
                        {
                            if (args.empty())
                            {}
                            else
                                tracepid(atoi(args[0].c_str()));
                        }
                        else if (command == "tracet")
                        {
                            if (args.empty())
                            {}
                            else
                                tracetid(atoi(args[0].c_str()));
                        }
#endif
                        else
                        {
                            ExecuteCommand(sys, command, args);
                        }
                    }
                }
            }
        }
    }
    catch (const exception& e)
    {
        PrintException(cerr, e);
        exit(1);
    }
    exit(0);
}
