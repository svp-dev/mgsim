#ifdef HAVE_CONFIG_H
#include <sys_config.h>
#endif

#include "simreadline.h"
#include "commands.h"
#include <arch/MGSystem.h>
#include <sim/config.h>
#include <sim/configparser.h>
#include <sim/readfile.h>
#include <sim/rusage.h>
#include <sim/sampling.h>
#include <sim/monitor.h>

#include <sstream>
#include <iostream>
#include <fstream>
#include <limits>
#include <memory>
#include <cstdlib>

#include <sys/param.h>
#include <unistd.h>

#ifdef USE_SDL
#include <SDL.h>
#endif

#include <argp.h>

using namespace Simulator;
using namespace std;

struct ProgramConfig
{
    unsigned int                     m_areaTech;
    string                           m_configFile;
    bool                             m_enableMonitor;
    bool                             m_interactive;
    bool                             m_terminate;
    bool                             m_dumpconf;
    bool                             m_dumpcache;
    bool                             m_quiet;
    bool                             m_dumpvars;
    vector<string>                   m_printvars;
    bool                             m_earlyquit;
    ConfigMap                        m_overrides;
    vector<string>                   m_extradevs;
    vector<string>                   m_regs;
    bool                             m_dumptopo;
    string                           m_topofile;
    bool                             m_dumpnodeprops;
    bool                             m_dumpedgeprops;
    vector<string>                   m_argv;
    ProgramConfig()
        : m_areaTech(0),
          m_configFile(MGSIM_CONFIG_PATH),
          m_enableMonitor(false),
          m_interactive(false),
          m_terminate(false),
          m_dumpconf(false),
          m_dumpcache(false),
          m_quiet(false),
          m_dumpvars(false),
          m_printvars(),
          m_earlyquit(false),
          m_overrides(),
          m_extradevs(),
          m_regs(),
          m_dumptopo(false),
          m_topofile(),
          m_dumpnodeprops(true),
          m_dumpedgeprops(true),
          m_argv()
    {
        const char *v = getenv("MGSIM_BASE_CONFIG");
        if (v != nullptr)
        {
            // A base file in the environment variable
            // will override the default.
            m_configFile = v;
        }
    }
};

extern "C"
{
const char *argp_program_version =
    "mgsim " PACKAGE_VERSION "\n"
    "Copyright (C) 2008,2009,2010,2011,2012,2013 the MGSim project.\n"
    "\n"
    "Written by Mike Lankamp. Maintained by the MGSim project.";

const char *argp_program_bug_address =
    PACKAGE_BUGREPORT;
}

static const char *mgsim_doc =
    "This program runs micro-architecture simulation models."
    "\v" /* separates top and bottom part of --help generated by argp. */
    "The first non-option argument is treated as a file to load as "
    "a bootable ROM. All non-option arguments are also stored as strings "
    "in a data ROM. For more advanced code/data arrangements, use "
    "configuration options to set up ROM devices and memory ranges."
    "\n\n"
    "If argument -c is not specified, the base configuration file "
    "is taken from environment variable MGSIM_BASE_CONFIG if set, "
    "otherwise from " MGSIM_CONFIG_PATH "."
    "\n\n"
    "For more information, see mgsimdoc(1).";

static const struct argp_option mgsim_options[] =
{
    { "interactive", 'i', 0, 0, "Start the simulator in interactive mode.", 0 },

    { 0, 'R', "NUM VALUE", 0, "Store the integer VALUE in the specified register of the initial thread.", 1 },
    { 0, 'F', "NUM VALUE", 0, "Store the float VALUE in the specified FP register of the initial thread.", 1 },
    { 0, 'L', "NUM FILE", 0, "Create an ActiveROM component with the contents of FILE and store the address in the specified register of the initial thread.", 1 },

    { "config", 'c', "FILE", 0, "Read default configuration from FILE. "
      "The contents of this file are considered after all overrides (-o/-I).", 2 },
    { "dump-configuration", 'd', 0, 0, "Dump configuration to standard error prior to program startup.", 2 },
    { "dump-config-cache", 10, 0, 0, "Dump configuration cache to standard error prior to program startup.", 2 },
    { "override", 'o', "NAME=VAL", 0, "Add override option NAME with value VAL. Can be specified multiple times.", 2 },
    { "include", 'I', "FILE", 0, "Read extra override options from FILE. Can be specified multiple times.", 2 },

    { "do-nothing", 'n', 0, 0, "Exit before the program starts, but after the system is configured.", 3 },
    { "quiet", 'q', 0, 0, "Do not print simulation statistics after execution.", 3 },
    { "terminate", 't', 0, 0, "Terminate the simulator upon an exception, instead of dropping to the interactive prompt.", 3 },

#ifdef ENABLE_CACTI
    { "area", 'a', "VAL", 0, "Dump area information prior to program startup using CACTI. Assume technology is VAL nanometers.", 4 },
#endif

    { "list-mvars", 'l', 0, 0, "Dump list of monitor variables prior to program startup.", 5 },
    { "print-final-mvars", 'p', "PATTERN", 0, "Print the value of all monitoring variables matching PATTERN. Can be specified multiple times.", 5 },

    { "dump-topology", 'T', "FILE", 0, "Dump the grid topology to FILE prior to program startup.", 6 },
    { "no-node-properties", 11, 0, 0, "Do not print component properties in the topology dump.", 6 },
    { "no-edge-properties", 12, 0, 0, "Do not print link properties in the topology output.", 6 },

    { "monitor", 'm', 0, 0, "Enable asynchronous simulation monitoring (configure with -o MonitorSampleVariables).", 7 },

    { "symtable", 's', "FILE", OPTION_HIDDEN, "(obsolete; symbols are now read automatically from ELF)", 8 },

    { 0, 0, 0, 0, 0, 0 }
};

static error_t mgsim_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct ProgramConfig &config = *(struct ProgramConfig*)state->input;

    switch (key)
    {
    case 'a':
    {
        char* endptr;
        unsigned int tech = strtoul(arg, &endptr, 0);
        if (*endptr != '\0') {
            throw runtime_error("Error: unable to parse technology size");
        } else if (tech < 1) {
            throw runtime_error("Error: technology size must be >= 1 nm");
        } else {
            config.m_areaTech = tech;
        }
    }
    break;
    case 'c': config.m_configFile = arg; break;
    case 'i': config.m_interactive = true; break;
    case 't': config.m_terminate = true; break;
    case 'q': config.m_quiet = true; break;
    case 's': cerr << "# Warning: ignoring obsolete flag '-s'" << endl; break;
    case 'd': config.m_dumpconf = true; break;
    case 10 : config.m_dumpcache = true; break;
    case 'm': config.m_enableMonitor = true; break;
    case 'l': config.m_dumpvars = true; break;
    case 'p': config.m_printvars.push_back(arg); break;
    case 'T': config.m_dumptopo = true; config.m_topofile = arg; break;
    case 11 : config.m_dumpnodeprops = false; break;
    case 12 : config.m_dumpedgeprops = false; break;
    case 'n': config.m_earlyquit = true; break;
    case 'o':
    {
            string sarg = arg;
            string::size_type eq = sarg.find_first_of("=");
            if (eq == string::npos) {
                throw runtime_error("Error: malformed configuration override syntax: " + sarg);
            }
            string name = sarg.substr(0, eq);

            config.m_overrides.append(name, sarg.substr(eq + 1));
    }
    break;
    case 'I':
    {
        ConfigParser parser(config.m_overrides);
        try {
            parser(read_file(arg));
        } catch (runtime_error& e) {
            throw runtime_error("Error reading include file: " + string(arg) + "\n" + e.what());
        }
    }
    break;
    case 'L':
    {
        string regnum(arg);
        if (state->next == state->argc) {
            throw runtime_error("Error: -L" + regnum + " expected filename");
        }
        string filename(state->argv[state->next++]);

        string devname = "rom_file" + regnum;
        config.m_extradevs.push_back(devname);
        string cfgprefix = devname + ":";
        config.m_overrides.append(cfgprefix + "Type", "AROM");
        config.m_overrides.append(cfgprefix + "ROMContentSource", "RAW");
        config.m_overrides.append(cfgprefix + "ROMFileName", filename);
        config.m_regs.push_back("R" + regnum + "=B" + devname);
    }
    break;
    case 'R': case 'F':
    {
        string regnum;
        regnum += (char)key;
        regnum += arg;
        if (state->next == state->argc) {
            throw runtime_error("Error: -" + regnum + ": expected register value");
        }

        config.m_regs.push_back(regnum + "=" + state->argv[state->next++]);
    }
    break;
    case ARGP_KEY_ARG: /* extra arguments */
    {
        config.m_argv.push_back(arg);
    }
    break;
    case ARGP_KEY_NO_ARGS:
        argp_usage (state);
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}


static struct argp argp = {
    mgsim_options /* options */,
    mgsim_parse_opt /* parser */,
    NULL /* args_doc */,
    mgsim_doc /* doc */,
    NULL /* children */,
    NULL /* help filter */,
    NULL /* argp domain */
};

static
void PrintFinalVariables(const ProgramConfig& cfg)
{
    if (!cfg.m_printvars.empty())
    {
        cout << "### begin end-of-simulation variables" << endl;
        for (auto& i : cfg.m_printvars)
            ReadSampleVariables(cout, i);
        cout << "### end end-of-simulation variables" << endl;
    }
}

static
void AtEnd(const MGSystem& sys, const ProgramConfig& cfg)
{
    if (!cfg.m_quiet)
    {
        clog << "### begin end-of-simulation statistics" << endl;
        sys.PrintAllStatistics(clog);
        clog << "### end end-of-simulation statistics" << endl;
    }
    PrintFinalVariables(cfg);
}

static
void MemoryExhausted()
{
    ResourceUsage ru(true);

    cerr << "MGSim: cannot allocate memory for C++ object (std::bad_alloc)." << endl
         << dec
         << "### error statistics" << endl
         << ru.GetUserTime() << "\t# total real time in user mode (us)" << endl
         << ru.GetSystemTime() << "\t# total real time in system mode (us)" << endl
         << ru.GetMaxResidentSize() << "\t# maximum resident set size (Kibytes)" << endl
         << "### end error statistics" << endl;
    std::abort();
}

#ifdef USE_SDL
extern "C"
#endif
int main(int argc, char** argv)
{
    std::set_new_handler(MemoryExhausted);

    ProgramConfig flags;
    UNIQUE_PTR<Config> config;
    UNIQUE_PTR<MGSystem> sys;
    UNIQUE_PTR<Monitor> mo;

    ////
    // Early initialization.
    // Argument parsing, no simulation yet.
    try
    {
        // Parse command line arguments
        argp_parse(&argp, argc, argv, 0, 0, &flags);
    }
    catch (const exception& e)
    {
        PrintException(NULL, cerr, e);
        return 1;
    }

    if (flags.m_interactive)
    {
        // Interactive mode: print name & version first
        clog << argp_program_version << endl
             << endl;

        // Then print also command name & arguments
        clog  << "Command line:";
        for (int i = 0; i < argc; ++i)
            clog << ' ' << argv[i];
        char buf[MAXPATHLEN];
        getcwd(buf, MAXPATHLEN);
        clog << endl << "Current working directory: " << buf << endl;
    }

    // Convert the remaining m_regs to an override
    if (!flags.m_regs.empty())
    {
        ostringstream s;
        for (size_t i = 0; i < flags.m_regs.size(); ++i)
        {
            if (i)
                s << ',';
            s << flags.m_regs[i];
        }
        flags.m_overrides.append("CmdLineRegs", s.str());
    }

    // Convert the extra devices created with -L to an override
    if (!flags.m_extradevs.empty())
    {
        string n;
        for (size_t i = 0; i < flags.m_extradevs.size(); ++i)
        {
            if (i > 0)
                n += ',';
            n += flags.m_extradevs[i];
        }
        flags.m_overrides.append("CmdLineFileDevs", n);
    }

    if (flags.m_quiet)
    {
        // Silence the ROM loads.
        flags.m_overrides.append("*.ROMVerboseLoad", "false");
    }

    ////
    // Load the simulation configuration.
    // Process -c, group with overrides and argv.
    try
    {
        // Read configuration from file
        ConfigMap base_config;
        ConfigParser parser(base_config);
        try {
            parser(read_file(flags.m_configFile));
        } catch (runtime_error& e) {
            throw runtime_error("Error reading configuration file: " + flags.m_configFile + "\n" + e.what());
        }

        config.reset(new Config(base_config, flags.m_overrides, flags.m_argv));
    }
    catch (const exception& e)
    {
        PrintException(NULL, cerr, e);
        return 1;
    }

    if (flags.m_dumpconf)
    {
        // Printing the configuration if requested. We need to
        // do/check this early, in case constructing the system
        // (below) fails.
        clog << "### simulator version: " PACKAGE_VERSION << endl;
        config->dumpConfiguration(clog, flags.m_configFile);
    }

    srand(config->getValueOrDefault<unsigned>("RandomSeed", (unsigned)time(NULL)));


    ////
    // Construct the simulator.
    // This instantiates all the components, in the initial (stopped) state.
    // It also populates the "config" object with a cache of all
    // values effectively looked up by the instantiated components.
    try
    {
        // Create the system
        sys.reset(new MGSystem(*config, !flags.m_interactive));
    }
    catch (const exception& e)
    {
        PrintException(NULL, cerr, e);
        return 1;
    }

    if (flags.m_dumpcache)
    {
        // Dump the configuration cache if requested.
        config->dumpConfigurationCache(clog);
    }

    if (flags.m_dumpvars)
    {
        // Dump the list of monitoring variables if requested.
        clog << "### begin monitor variables" << endl;
        ListSampleVariables(clog);
        clog << "### end monitor variables" << endl;
    }

    if (flags.m_areaTech > 0)
    {
        // Dump the area estimation information if requested.
        clog << "### begin area information" << endl;
#ifdef ENABLE_CACTI
        sys->DumpArea(cout, flags.m_areaTech);
#else
        clog << "# Warning: CACTI not enabled; reconfigure with --enable-cacti" << endl;
#endif
        clog << "### end area information" << endl;
    }

    if (flags.m_dumptopo)
    {
        // Dump the component topology diagram if requested.
        ofstream of(flags.m_topofile.c_str(), ios::out);
        config->dumpComponentGraph(of, flags.m_dumpnodeprops, flags.m_dumpedgeprops);
        of.close();
    }

    if (flags.m_earlyquit)
        // At this point the simulation is ready and we have dumped
        // everything requested. If the user requested to not do anything,
        // we can just stop here.
        return 0;

    // Otherwise, the simulation should start.

    ////
    // Start the simulation.

    // First construct the monitor thread, if enabled.
    string mo_mdfile = config->getValueOrDefault<string>("MonitorMetadataFile", "mgtrace.md");
    string mo_tfile = config->getValueOrDefault<string>("MonitorTraceFile", "mgtrace.out");
    mo.reset(new Monitor(*sys, flags.m_enableMonitor,
                         mo_mdfile, flags.m_earlyquit ? "" : mo_tfile, !flags.m_interactive));

    // Simulation proper.
    // Rules:
    // - if interactive, then do not automatically start the simulation.
    // - if interactive at start, always remain interactive.
    // - if not interactive at start and the simulation stops abnormally, then become interactive unless -t is specified.
    // - upon becoming interative because of an exception, print the exception.
    // - always print statistics at termination if not quiet.
    // - always print final variables at termination.

    bool interactive = flags.m_interactive;
    try
    {
        if (!interactive)
        {
            // Non-interactive: automatically start and run until simulation terminates.
            try
            {
                mo->start();
                StepSystem(*sys, INFINITE_CYCLES);
                mo->stop();

            }
            catch (const exception& e)
            {
                mo->stop();
                if (flags.m_terminate)
                {
                    // Re-throw to terminate.
                    throw;
                }

                // else
                // Print the exception and become interactive.
                PrintException(sys.get(), cerr, e);
                interactive = true;
            }
        }

        if (interactive)
        {
            // Command loop
            cout << endl;
            CommandLineReader clr;
            cli_context ctx = { clr, *sys, *mo };

            while (HandleCommandLine(ctx) == false)
                /* just loop */;
        }
    }
    catch (const exception& e)
    {
        const ProgramTerminationException *ex = dynamic_cast<const ProgramTerminationException*>(&e);

        if (!flags.m_quiet || ex == NULL)
            // Print exception.
            PrintException(sys.get(), cerr, e);

        // Print statistics & final variables.
        AtEnd(*sys, flags);

        if (ex != NULL)
        {
            // The program is telling us how to terminate. Do it.
            if (ex->TerminateWithAbort())
                abort();
            else
                return ex->GetExitCode();
        }
        else
            // No more information, simply terminate with error.
            return 1;
    }

    // Print statistics & final variables.
    AtEnd(*sys, flags);

    return 0;
}
