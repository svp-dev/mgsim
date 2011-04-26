#include "commands.h"
#include "sim/sampling.h"

#include "sim/inspect.h"

#include "arch/mem/SerialMemory.h"
#include "arch/mem/ParallelMemory.h"
#include "arch/mem/BankedMemory.h"
#include "arch/mem/RandomBankedMemory.h"
#include "arch/mem/coma/COMA.h"
#include "arch/mem/coma/Cache.h"
#include "arch/mem/coma/Directory.h"
#include "arch/mem/coma/RootDirectory.h"
#include "arch/mem/zlcoma/COMA.h"
#include "arch/mem/zlcoma/Cache.h"
#include "arch/mem/zlcoma/Directory.h"
#include "arch/mem/zlcoma/RootDirectory.h"
#include "arch/dev/NullIO.h"
#include "arch/proc/Processor.h"

#include <cerrno>
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

template<unsigned Type>
void ExecuteCommand_(std::ostream& out, MGSystem& sys, const string& cmd, vector<string>& args)
{
    if (args.size() == 0)
    {
        out << "Please specify a component. Use \"print\" for a list of all components" << endl;
        return;
    }
    
    string name = args.front();
    Object* obj = sys.GetComponent(name);
    if (obj == NULL)
    {
        out << "Component not found: " << name << endl;
        return;
    }
    
    Inspect::Interface_<Type> * interface = dynamic_cast<Inspect::Interface_<Type>*>(obj);
    if (interface == NULL)
    {
        out << "Component " << name << " does not support command \"" << cmd << '"' << endl;
        return;
    }
    
    args.erase(args.begin());
    
    stringstream backup;
    backup.copyfmt(cout);
    
    interface->DoCommand(out, args);
    
    out << endl;
    
    if (Type == Inspect::Help)
    {
        out << "Supported commands: ";
        Inspect::ListCommands *lc = dynamic_cast<Inspect::ListCommands*>(obj);
        assert(lc != NULL);
        lc->ListSupportedCommands(out);
        out << endl;
    }
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
            string pat = "*";
            if (!args.empty())
                pat = args[0];
            size_t levels = 0;
            if (args.size() > 1)
            {
                levels = strtoul(args[1].c_str(), 0, 0);
            }
            sys.PrintComponents(std::cout, pat, levels);
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
        else if(command == "devicedb")
        {
            DeviceDatabase::GetDatabase().Print(std::cout);
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
            else if (state == "IO")       sys.ToggleDebugMode(Kernel::DEBUG_IO);
            else if (state == "REG")      sys.ToggleDebugMode(Kernel::DEBUG_REG);
            else if (state == "ALL")      sys.SetDebugMode(-1);
            else if (state == "NONE")     sys.SetDebugMode(0);
                
            string debugStr;
            int m = sys.GetDebugMode();
            if (m & Kernel::DEBUG_PROG)     debugStr += " program";
            if (m & Kernel::DEBUG_SIM)      debugStr += " simulator";
            if (m & Kernel::DEBUG_DEADLOCK) debugStr += " deadlocks";
            if (m & Kernel::DEBUG_FLOW)     debugStr += " flow";
            if (m & Kernel::DEBUG_MEM)      debugStr += " memory";
            if (m & Kernel::DEBUG_IO)       debugStr += " io";
            if (m & Kernel::DEBUG_REG)      debugStr += " registers";
            if (!debugStr.size()) debugStr = " (nothing)";
            cout << "Debugging:" << debugStr << endl;
        }
        else if ((command == "d" || command == "dis" || command == "disasm") && !args.empty())
        {
            MemAddr addr = strtoull(args[0].c_str(), 0, 0);
            if (errno == EINVAL)
            {
                bool check = sys.GetKernel().GetSymbolTable().LookUp(args[0], addr, true);
                if (!check)
                {
                    cout << "invalid address: " << args[0] << endl;
                    break;
                }
            }

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
        else if (command == "help") { ExecuteCommand_<Inspect::Help>(cout, sys, command, args); }
        else if (command == "read") { ExecuteCommand_<Inspect::Read>(cout, sys, command, args); }
        else if (command == "info") { ExecuteCommand_<Inspect::Info>(cout, sys, command, args); }
        else if (command == "line") { ExecuteCommand_<Inspect::Line>(cout, sys, command, args); }
        else if (command == "trace") { ExecuteCommand_<Inspect::Trace>(cout, sys, command, args); }
        else
        {
            cerr << "Unknown command: " << command << endl;
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
        "Microgrid Simulator.\n\n"
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
