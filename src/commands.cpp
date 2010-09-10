#include "commands.h"
#include "sampling.h"

#include "SerialMemory.h"
#include "ParallelMemory.h"
#include "BankedMemory.h"
#include "RandomBankedMemory.h"
#include "coma/COMA.h"
#include "coma/Cache.h"
#include "coma/Directory.h"
#include "coma/RootDirectory.h"

#include <csignal>
#include <sstream>
#include <iomanip>

using namespace Simulator;
using namespace std;

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

class bind_cmd
{
public:
    virtual bool call(std::ostream& out, Object* obj, const std::vector<std::string>& arguments) const = 0;
    virtual ~bind_cmd() {}
};

// Const version
template <typename T>
class bind_cmd_C : public bind_cmd
{
    typedef void (T::*func_t)(std::ostream& out, const std::vector<std::string>& arguments) const;
    func_t m_func;
public:
    bool call(std::ostream& out, Object* obj, const std::vector<std::string>& arguments) const {
        const T* o = dynamic_cast<T*>(obj);
        if (o == NULL) return false;
        (o->*m_func)(out, arguments);
        return true;
    }
    bind_cmd_C(const func_t& func) : m_func(func) {}
};

// Non-const version
template <typename T>
class bind_cmd_NC : public bind_cmd
{
    typedef void (T::*func_t)(std::ostream& out, const std::vector<std::string>& arguments);
    func_t m_func;
public:
    bool call(std::ostream& out, Object* obj, const std::vector<std::string>& arguments) const {
        T* o = dynamic_cast<T*>(obj);
        if (o == NULL) return false;
        (o->*m_func)(out, arguments);
        return true;
    }
    bind_cmd_NC(const func_t& func) : m_func(func) {}
};

static const struct
{
    const char*     name;
    const bind_cmd* func;
} _Commands[] = {
    {"help", new bind_cmd_C<RAUnit            >(&RAUnit            ::Cmd_Help) },
    {"help", new bind_cmd_C<ThreadTable       >(&ThreadTable       ::Cmd_Help) },
    {"help", new bind_cmd_C<FamilyTable       >(&FamilyTable       ::Cmd_Help) },
    {"help", new bind_cmd_C<Network           >(&Network           ::Cmd_Help) },
    {"help", new bind_cmd_C<RegisterFile      >(&RegisterFile      ::Cmd_Help) },
    {"help", new bind_cmd_C<ICache            >(&ICache            ::Cmd_Help) },
    {"help", new bind_cmd_C<DCache            >(&DCache            ::Cmd_Help) },
    {"help", new bind_cmd_C<Pipeline          >(&Pipeline          ::Cmd_Help) },
    {"help", new bind_cmd_C<Allocator         >(&Allocator         ::Cmd_Help) },
    {"help", new bind_cmd_C<SerialMemory      >(&SerialMemory      ::Cmd_Help) },
    {"help", new bind_cmd_C<ParallelMemory    >(&ParallelMemory    ::Cmd_Help) },
    {"help", new bind_cmd_C<RandomBankedMemory>(&RandomBankedMemory::Cmd_Help) },
    {"help", new bind_cmd_C<BankedMemory      >(&BankedMemory      ::Cmd_Help) },
    {"help", new bind_cmd_C<COMA              >(&COMA               ::Cmd_Help) },
    {"help", new bind_cmd_C<COMA::Cache       >(&COMA::Cache        ::Cmd_Help) },
    {"help", new bind_cmd_C<COMA::Directory   >(&COMA::Directory    ::Cmd_Help) },
    {"help", new bind_cmd_C<COMA::RootDirectory>(&COMA::RootDirectory::Cmd_Help) },
    {"help", new bind_cmd_C<FPU               >(&FPU               ::Cmd_Help) },
    {"info", new bind_cmd_C<VirtualMemory     >(&VirtualMemory     ::Cmd_Info) },
    {"line", new bind_cmd_C<COMA              >(&COMA              ::Cmd_Line) },
    {"read", new bind_cmd_C<RAUnit            >(&RAUnit            ::Cmd_Read) },
    {"read", new bind_cmd_C<ThreadTable       >(&ThreadTable       ::Cmd_Read) },
    {"read", new bind_cmd_C<FamilyTable       >(&FamilyTable       ::Cmd_Read) },
    {"read", new bind_cmd_C<Network           >(&Network           ::Cmd_Read) },
    {"read", new bind_cmd_C<RegisterFile      >(&RegisterFile      ::Cmd_Read) },
    {"read", new bind_cmd_C<ICache            >(&ICache            ::Cmd_Read) },
    {"read", new bind_cmd_C<DCache            >(&DCache            ::Cmd_Read) },
    {"read", new bind_cmd_C<Pipeline          >(&Pipeline          ::Cmd_Read) },
    {"read", new bind_cmd_C<Allocator         >(&Allocator         ::Cmd_Read) },
    {"read", new bind_cmd_C<SerialMemory      >(&SerialMemory      ::Cmd_Read) },
    {"read", new bind_cmd_C<ParallelMemory    >(&ParallelMemory    ::Cmd_Read) },
    {"read", new bind_cmd_C<RandomBankedMemory>(&RandomBankedMemory::Cmd_Read) },
    {"read", new bind_cmd_C<BankedMemory      >(&BankedMemory      ::Cmd_Read) },
    {"read", new bind_cmd_C<COMA::Cache       >(&COMA::Cache        ::Cmd_Read) },
    {"read", new bind_cmd_C<COMA::Directory   >(&COMA::Directory    ::Cmd_Read) },
    {"read", new bind_cmd_C<COMA::RootDirectory>(&COMA::RootDirectory::Cmd_Read) },
    {"read", new bind_cmd_C<VirtualMemory     >(&VirtualMemory     ::Cmd_Read) },
    {"read", new bind_cmd_C<FPU               >(&FPU               ::Cmd_Read) },
    {"trace", new bind_cmd_NC<COMA             >(&COMA              ::Cmd_Trace) },
    {NULL, NULL}
};

void ExecuteCommand(MGSystem& sys, const string& command, const vector<string>& _args)
{
    // Backup stream state before command
    vector<string> args = _args;
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

static vector<string> prevCommands;


void HandleCommandLine(CommandLineReader& clr,
                       MGSystem& sys,
#ifdef ENABLE_MONITOR
                       Monitor& mo,
#endif
                       bool &quit)
{
    stringstream prompt;
    prompt << dec << setw(8) << setfill('0') << right << sys.GetKernel().GetCycleNo() << "> ";
            
    // Read the command line and split into commands
    char* line = clr.GetCommandLine(prompt.str());
    if (line == NULL)
    {
        // End of input
        cout << endl;
        quit = true;
        return ;
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
    for (vector<string>::const_iterator command = commands.begin(); 
         command != commands.end() && !quit; 
         ++command)
    {
        vector<string> args = Tokenize(Trim(*command), " ");
        if (args.size() == 0)
            continue;

        // Pop the command from the front
        string command = args[0];
        args.erase(args.begin());
        
        if (command == "h" || command == "/?" || (command == "help" && args.empty()))
        {
            PrintHelp(cout);
        }
        else if (command == "r" || command == "run" || command == "s" || command == "step" || command == "c" || command == "continue")
        {
            // Step of run
            CycleNo nCycles = INFINITE_CYCLES;
            if (command[0] == 's')
            {
                // Step
                nCycles = args.empty() ? 1 : max(1UL, strtoul(args[0].c_str(), 0, 0));
            }
                
            // Flush the history, in case something
            // bad happens in StepSystem...
            clr.CheckPointHistory();
                
#ifdef ENABLE_MONITOR
            mo.start();
#endif
            try
            {
                StepSystem(sys, nCycles);
            }
            catch (const exception& e)
            {
                PrintException(cerr, e);
            }
#ifdef ENABLE_MONITOR
            mo.stop();
#endif
        }
        else if (command == "b" || command == "break")
        {
            if (!args.empty())
            {
                int offset = 0;
#if TARGET_ARCH == ARCH_ALPHA
                // skip 8 bytes for the ldgp instruction
                offset = 8;
#endif
                sys.GetKernel().GetBreakPoints().AddBreakPoint(args[0], offset);
            }
            else
                sys.GetKernel().GetBreakPoints().ReportBreaks(cout);
        }
        else if(command == "bp" && !args.empty())
        {
            BreakPoints& bp = sys.GetKernel().GetBreakPoints();
            if (args[0] == "add" && args.size() == 3)
            {
                int mode = 0;
                for (string::const_iterator i = args[2].begin(); i != args[2].end(); ++i)
                {
                    if (toupper(*i) == 'R') mode |= BreakPoints::READ;
                    if (toupper(*i) == 'W') mode |= BreakPoints::WRITE;
                    if (toupper(*i) == 'X') mode |= BreakPoints::EXEC;
                    if (toupper(*i) == 'T') mode |= BreakPoints::TRACEONLY;
                }
                if (mode == 0)
                    cout << "Invalid breakpoint mode:" << args[2] << endl;
                else
                    bp.AddBreakPoint(args[1], 0, mode);                
            }
            else if (args.size() == 1)
            {
                if (args[0] == "list")
                    bp.ListBreakPoints(std::cout);
                else if (args[0] == "on")
                    bp.EnableCheck();
                else if (args[0] == "off")
                    bp.DisableCheck();
                else if (args[0] == "clear")
                    bp.ClearAllBreakPoints();
                else
                    cout << "Unknown command" << endl;
            }
            else if (args.size() == 2)
            {
                unsigned id = strtoul(args[1].c_str(), 0, 0);
                if (args[0] == "del")
                    bp.DeleteBreakPoint(id);
                else if (args[0] == "enable")
                    bp.EnableBreakPoint(id);
                else if (args[0] == "disable")
                    bp.DisableBreakPoint(id);
                else
                    cout << "Unknown command" << endl;
            }
            else
                cout << "Unknown command" << endl;
        }
        else if (command == "p" || command == "print")
        {
            sys.PrintComponents(std::cout);
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
        else if (command == "stat")
        {
            sys.PrintAllStatistics(std::cout);
        }
        else if (command == "samplevars")
        {
            string pat = "*";
            if (!args.empty())
                pat = args[0];
            ListSampleVariables(std::cout, pat);
        }
        else if(command == "pv" && !args.empty())
        {
            ShowSampleVariables(std::cout, args[0]);
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
            else if (state == "FLOW")     sys.ToggleDebugMode(Kernel::DEBUG_FLOW);
            else if (state == "MEM")      sys.ToggleDebugMode(Kernel::DEBUG_MEM);
            else if (state == "ALL")      sys.SetDebugMode(-1);
            else if (state == "NONE")     sys.SetDebugMode(0);
                
            string debugStr;
            int m = sys.GetDebugMode();
            if (m & Kernel::DEBUG_PROG)     debugStr += " program";
            if (m & Kernel::DEBUG_SIM)      debugStr += " simulator";
            if (m & Kernel::DEBUG_DEADLOCK) debugStr += " deadlocks";
            if (m & Kernel::DEBUG_FLOW)     debugStr += " flow";
            if (m & Kernel::DEBUG_MEM)      debugStr += " memory";
            if (!debugStr.size()) debugStr = " (nothing)";
            cout << "Debugging:" << debugStr << endl;
        }
        else if ((command == "d" || command == "dis" || command == "disasm") && !args.empty())
        {
            MemAddr addr = strtoull(args[0].c_str(), 0, 0);
            size_t sz = 0;
            if (args.size() > 1)
                sz = strtoul(args[1].c_str(), 0, 0);
            if (sz)
                sys.Disassemble(addr, sz);
            else
                sys.Disassemble(addr);
        }
        else if (command == "l" || command == "list")
        {
            if (args.empty())
                sys.PrintAllSymbols(cout);
            else
                sys.PrintAllSymbols(cout, args[0]);
        }
        else if ((command == "f" || command == "find") && !args.empty())
        {
            cout << sys.GetSymbol(static_cast<MemAddr>(strtoull(args[0].c_str(), 0, 0))) << endl;
        }
        else
        {
            ExecuteCommand(sys, command, args);
        }
    }
}


void PrintVersion(std::ostream& out)
{
    out << 
        "mgsim (Microgrid Simulator) " PACKAGE_VERSION "\n"
        "Copyright (C) 2009 Universiteit van Amsterdam.\n"
        "\n"
        "Written by Mike Lankamp. Contributions by Li Zhang, Raphael 'kena' Poss.\n";
}

void PrintUsage(std::ostream& out, const char* cmd)
{
    out <<
        "Microgrid Simulator"
#ifdef ENABLE_COMA_ZL
        " with ZL's COMA memory enabled"
#endif
        ".\n"
        "Each simulated core implements the " CORE_ISA_NAME " ISA.\n\n"
        "Usage: " << cmd << " [ARG]...\n\n"
        "Options:\n\n"
        "  -c, --config FILE        Read configuration from FILE.\n"
        "  -d, --dumpconf           Dump configuration to standard error prior to program startup.\n"
        "  -D, --dumpvars           Dump list of monitor variables prior to program startup.\n"
        "  -n, --do-nothing         Exit before the program starts, but after the system is configured.\n"
        "  -i, --interactive        Start the simulator in interactive mode.\n"
        "  -o, --override NAME=VAL  Overrides the configuration option NAME with value VAL.\n"
        "  -q, --quiet              Do not print simulation statistics after execution.\n" 
        "  -s, --symtable FILE      Read symbol from FILE. (generate with nm -P)\n"
        "  -t, --terminate          Terminate the simulator upon an exception.\n"
#ifdef ENABLE_MONITOR
        "  -m, --monitor            Enable simulation monitoring.\n"
#endif
        "  -R<X> VALUE              Store the integer VALUE in the specified register.\n"
        "  -F<X> VALUE              Store the float VALUE in the specified FP register.\n"
        "  -L<X> FILE               Load the contents of FILE after the program in memory\n" 
        "                           and store the address in the specified register.\n" 
        "Other options:\n"          
        "  -h, --help               Print this help, then exit.\n"
        "      --version            Print version information, then exit.\n"
        "\n"
        "Report bugs and suggestions to " PACKAGE_BUGREPORT ".\n";
}

// Prints the help text
void PrintHelp(ostream& out)
{
    out <<
        "Available commands:\n"
        "-------------------\n"
        "(h)elp               Print this help text.\n"
        "(p)rint              Print all components in the system.\n"
        "(s)tep               Advance the system one clock cycle.\n"
        "(r)un/(c)ontinue     Run the system until it is idle or deadlocks.\n"
        "                     Livelocks will not be reported.\n"
        "(f)ind <addr>        Find the symbol nearest to the specified address.\n"
        "(d)isasm <addr>      Disassemble program from this address.\n"
        "(l)ist [PAT]         List symbols matching PAT (default *).\n"
        "\n"
        "(b)reak <addr>       Set exec breakpoint at this address.\n"
        "bp on/off            Enable/disable breakpoint checking.\n"
        "bp add <addr> <mode> Add a breakpoint for this address.\n"
        "                     Mode is a combination of R, W, or X.\n"
        "bp list              List existing breakpoints.\n"
        "bp disable <id>      Disable specified breakpoint.\n"
        "bp enable <id>       Enable specified breakpoint.\n"
        "bp del <id>          Delete specified breakpoint.\n"
        "bp clear             Delete all breakpoints.\n"
        "\n"
        "samplevars [PAT]     List sample variables matching PAT (default *).\n"
        "pv PAT               Show current value of variables matching PAT.\n"
        "\n"
        "state                Shows the state of the system. Idle components\n"
        "                     are left out.\n"
        "debug [mode]         Show debug mode or set debug mode\n"
        "                     Debug mode can be: SIM, PROG, DEADLOCK or NONE.\n"
        "                     ALL is short for SIM and PROG\n"
        "help <component>     Show the supported methods and options for this\n"
        "                     component.\n"
        << endl;
}

void PrintException(ostream& out, const exception& e)
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

void StepSystem(MGSystem& system, CycleNo cycles)
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
