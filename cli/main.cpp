#ifdef HAVE_CONFIG_H
#include "sys_config.h"
#endif

#include "arch/MGSystem.h"
#include "simreadline.h"
#include "commands.h"
#include "sim/config.h"

#ifdef ENABLE_MONITOR
# include "sim/monitor.h"
#endif

#include <sstream>
#include <iostream>
#include <limits>

#ifdef USE_SDL
#include <SDL.h>
#endif

using namespace Simulator;
using namespace std;

struct ProgramConfig
{
    unsigned int                     m_areaTech;
    string                           m_configFile;
    string                           m_symtableFile;
    bool                             m_enableMonitor;
    bool                             m_interactive;
    bool                             m_terminate;
    bool                             m_dumpconf;
    bool                             m_quiet;
    bool                             m_dumpvars;
    vector<string>                   m_printvars;
    bool                             m_earlyquit;
    vector<pair<string,string> >     m_overrides;
    vector<string>                   m_extradevs;    
    vector<pair<RegAddr, string> >   m_loads;
    vector<pair<RegAddr, RegValue> > m_regs;
    bool                             m_dumptopo;
    string                           m_topofile;
    bool                             m_dumpnodeprops;
    bool                             m_dumpedgeprops;
    vector<string>                   m_argv;
};

static void ParseArguments(int argc, const char ** argv, ProgramConfig& config)
{
    config.m_areaTech = 0;
    config.m_configFile = MGSIM_CONFIG_PATH;
    config.m_enableMonitor = false;
    config.m_interactive = false;
    config.m_terminate = false;
    config.m_dumpconf = false;
    config.m_quiet = false;
    config.m_dumpvars = false;
    config.m_earlyquit = false;
    config.m_dumptopo = false;
    config.m_dumpnodeprops = true;
    config.m_dumpedgeprops = true;

    bool ignore_args = false;

    for (int i = 1; i < argc; ++i)
    {
        const string arg = argv[i];
        if (ignore_args || arg[0] != '-')
        {
            if (!ignore_args && config.m_argv.empty())
            {
                cerr << "Warning: converting first extra argument to -o *:ROMFileName=" << arg << endl;
                config.m_overrides.push_back(make_pair("*:ROMFileName", arg));
            }
            config.m_argv.push_back(arg);
        }
        else if (arg == "-a" || arg == "--area") 
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected technology size in nanometers");
            }
            
            char* endptr;
            unsigned int tech = strtoul(argv[i], &endptr, 0);
            if (*endptr != '\0') {
                throw runtime_error("Error: unable to parse technology size");
            } else if (tech < 1) {
                throw runtime_error("Error: technology size must be >= 1 nm");
            } else {
                config.m_areaTech = tech;
            }
        }
        else if (arg == "-c" || arg == "--config")
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected configuration filename");
            }
            config.m_configFile = argv[i];
        }
        else if (arg == "-i" || arg == "--interactive") config.m_interactive   = true;
        else if (arg == "-t" || arg == "--terminate")   config.m_terminate     = true;
        else if (arg == "-q" || arg == "--quiet")       config.m_quiet         = true;
        else if (arg == "-s" || arg == "--symtable")
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected symbol table filename");
            }
            config.m_symtableFile = argv[i];
        }
        else if (arg == "--version")                    { PrintVersion(std::cout); exit(0); }
        else if (arg == "-h" || arg == "--help")        { PrintUsage(std::cout, argv[0]); exit(0); }
        else if (arg == "-d" || arg == "--dump-configuration")    config.m_dumpconf      = true;
        else if (arg == "-m" || arg == "--monitor")     config.m_enableMonitor = true;
        else if (arg == "-l" || arg == "--list-mvars")  config.m_dumpvars      = true;
        else if (arg == "-p" || arg == "--print-final-mvars")
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected variable name");
            }
            config.m_printvars.push_back(argv[i]);
        }
        else if (arg == "-T" || arg == "--dump-topology")
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected file name");
            }
            config.m_dumptopo = true;
            config.m_topofile = argv[i];
        }
        else if (arg == "--no-node-properties") config.m_dumpnodeprops = false;
        else if (arg == "--no-edge-properties") config.m_dumpedgeprops = false;
        else if (arg == "-n" || arg == "--do-nothing")  config.m_earlyquit     = true;
        else if (arg == "-o" || arg == "--override")
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected configuration option");
            }
            string arg = argv[i];
            string::size_type eq = arg.find_first_of("=");
            if (eq == string::npos) {
                throw runtime_error("Error: malformed configuration override syntax: " + arg);
            }
            string name = arg.substr(0, eq);

            // push overrides in inverse order, so that the latest
            // specified in the command line has higher priority in
            // matching.
            config.m_overrides.insert(config.m_overrides.begin(), make_pair(name, arg.substr(eq + 1)));
        }
        else if (arg[1] == 'L')  
        { 
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected filename");
            }
            string filename(argv[i]);
            string regnum(arg, 2, arg.size());
            char* endptr; 
            unsigned long index = strtoul(regnum.c_str(), &endptr, 0); 
            if (*endptr != '\0') { 
                throw runtime_error("Error: invalid register specifier in option: " + arg); 
            } 
            RegAddr  regaddr = MAKE_REGADDR(RT_INTEGER, index);

            string devname = "file" + regnum;
            config.m_extradevs.push_back(devname);
            string cfgprefix = devname + ":";
            config.m_overrides.push_back(make_pair(cfgprefix + "Type", "AROM"));
            config.m_overrides.push_back(make_pair(cfgprefix + "ROMContentSource", "RAW"));
            config.m_overrides.push_back(make_pair(cfgprefix + "ROMFileName", filename));
            config.m_loads.push_back(make_pair(regaddr, devname)); 
        } 
        else if (arg[1] == 'R' || arg[1] == 'F')
        {
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected register value");
            }
            stringstream value;
            value << argv[i];

            RegAddr  addr;
            RegValue val;

            char* endptr;
            unsigned long index = strtoul(&arg[2], &endptr, 0);
            if (*endptr != '\0') {
                throw runtime_error("Error: invalid register specifier in option");
            }
                
            if (arg[1] == 'R') {
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
        else if (arg == "--")
        {
            // everything after "--" should be treated as extra (program)
            // eg to allow paths starting with "-".
            ignore_args = true;
        }
        else
        {
            throw runtime_error("Error: unknown command-line argument: " + arg);
        }
    }

    if (config.m_quiet)
    {
        config.m_overrides.push_back(make_pair("*.ROMVerboseLoad", "false"));
    }
}

void PrintFinalVariables(const ProgramConfig& cfg)
{
    if (!cfg.m_printvars.empty())
    {
        std::cout << "### begin end-of-simulation variables" << std::endl;
        for (size_t i = 0; i < cfg.m_printvars.size(); ++i)
            ReadSampleVariables(cout, cfg.m_printvars[i]);
        std::cout << "### end end-of-simulation variables" << std::endl;
    }
}

#ifdef USE_SDL
extern "C"
#endif
int main(int argc, char** argv)
{
    srand(time(NULL));
    
    try
    {
        // Parse command line arguments
        ProgramConfig config;
        ParseArguments(argc, (const char**)argv, config);

        if (config.m_interactive)
        {
            // Interactive mode
            PrintVersion(std::clog);
        }

        // Read configuration
        Config configfile(config.m_configFile, config.m_overrides, config.m_argv);

        if (config.m_dumpconf)
        {
            // Printing the configuration early, in case constructing the
            // system (below) fails.
            std::clog << "### simulator version: " PACKAGE_VERSION << std::endl;
            configfile.dumpConfiguration(std::clog, config.m_configFile);
        }
        
        // Create the system
        MGSystem sys(configfile, 
                     config.m_symtableFile,
                     config.m_regs, 
                     config.m_loads, 
                     config.m_extradevs, 
                     !config.m_interactive, 
                     !config.m_earlyquit);

#ifdef ENABLE_MONITOR
        string mo_mdfile = configfile.getValueOrDefault<string>("MonitorMetadataFile", "mgtrace.md");
        string mo_tfile = configfile.getValueOrDefault<string>("MonitorTraceFile", "mgtrace.out");
        Monitor mo(sys, config.m_enableMonitor, 
                   mo_mdfile, config.m_earlyquit ? "" : mo_tfile, !config.m_interactive);
#endif

        if (config.m_dumpconf)
        {
            // we also print the cache, which expands all effectively
            // looked up configuration values after the system
            // was constructed successfully.
            configfile.dumpConfigurationCache(std::clog);
        }
        if (config.m_dumpvars)
        {
            std::clog << "### begin monitor variables" << std::endl;
            ListSampleVariables(std::clog);
            std::clog << "### end monitor variables" << std::endl;
        }

        if (config.m_areaTech > 0)
        {
            std::clog << "### begin area information" << std::endl;
#ifdef ENABLE_CACTI
            sys.DumpArea(std::cout, config.m_areaTech);
#else
            std::clog << "# Warning: CACTI not enabled; reconfigure with --enable-cacti" << std::endl;
#endif
            std::clog << "### end area information" << std::endl;
        }

        if (config.m_dumptopo)
        {
            ofstream of(config.m_topofile.c_str(), ios::out);
            configfile.dumpComponentGraph(of, config.m_dumpnodeprops, config.m_dumpedgeprops);
            of.close();
        }

        if (config.m_earlyquit)
            exit(0);

        bool interactive = config.m_interactive;
        if (!interactive)
        {
            // Non-interactive mode; run and dump cycle count
            try
            {
#ifdef ENABLE_MONITOR
                mo.start();
#endif
                StepSystem(sys, INFINITE_CYCLES);
#ifdef ENABLE_MONITOR
                mo.stop();
#endif
                
                if (!config.m_quiet)
                {
                    clog << "### begin end-of-simulation statistics" << endl;
                    sys.PrintAllStatistics(clog);
                    clog << "### end end-of-simulation statistics" << endl;
                }
            }
            catch (const exception& e)
            {
#ifdef ENABLE_MONITOR
                mo.stop();
#endif
                if (config.m_terminate) 
                {
                    // We do not want to go to interactive mode,
                    // rethrow so it abort the program.
                    PrintFinalVariables(config);
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
            cout << endl;
            CommandLineReader clr;
            cli_context ctx = { clr, sys
#ifdef ENABLE_MONITOR
                                , mo 
#endif
            };

            while (HandleCommandLine(ctx) == false)
                /* just loop */;
        }

        PrintFinalVariables(config);
    }
    catch (const exception& e)
    {
        PrintException(cerr, e);
        return 1;
    }
    
    return 0;
}
