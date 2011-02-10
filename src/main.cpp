#ifdef HAVE_CONFIG_H
#include "sys_config.h"
#endif

#ifdef ENABLE_COMA_ZL
# include "coma/simlink/th.h"
# include "coma/simlink/linkmgs.h"
const char* semaphore_journal = "/tmp/simx-sem-journal";
#endif

#include "MGSystem.h"
#include "simreadline.h"
#include "commands.h"
#include "display.h"
#include "config.h"

#ifdef ENABLE_MONITOR
# include "monitor.h"
#endif

#include <sstream>
#include <iostream>
#include <limits>

#ifdef USE_SDL
#include <SDL.h>
#endif

using namespace Simulator;
using namespace std;
#ifdef ENABLE_COMA_ZL
using namespace MemSim;
#endif

struct ProgramConfig
{
    string             m_programFile;
    string             m_configFile;
    string             m_symtableFile;
    bool               m_enableMonitor;
    bool               m_interactive;
    bool               m_terminate;
    bool               m_dumpconf;
    bool               m_quiet;
    bool               m_dumpvars;
    bool               m_earlyquit;
    map<string,string> m_overrides;
    bool               m_openworld;
    bool               m_iowrite;
    
    vector<pair<RegAddr, RegValue> > m_regs;
    vector<pair<RegAddr, string> >   m_loads;
};

static void ParseArguments(int argc, const char ** argv, ProgramConfig& config
#ifdef ENABLE_COMA_ZL
               , LinkConfig& lkconfig
#endif
    )
{
    config.m_configFile = MGSIM_CONFIG_PATH;
    config.m_enableMonitor = false;
    config.m_interactive = false;
    config.m_terminate = false;
    config.m_dumpconf = false;
    config.m_quiet = false;
    config.m_dumpvars = false;
    config.m_earlyquit = false;
    config.m_openworld = false;
    config.m_iowrite = false;

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
        else if (arg == "-c" || arg == "--config")        config.m_configFile    = argv[++i];
        else if (arg == "-i" || arg == "--interactive")   config.m_interactive   = true;
        else if (arg == "-t" || arg == "--terminate")     config.m_terminate     = true;
        else if (arg == "-q" || arg == "--quiet")         config.m_quiet         = true;
        else if (arg == "-s" || arg == "--symtable")      config.m_symtableFile  = argv[++i];
        else if (arg == "--version")                      { PrintVersion(std::cout); exit(0); }
        else if (arg == "-h" || arg == "--help")          { PrintUsage(std::cout, argv[0]); exit(0); }
        else if (arg == "-d" || arg == "--dumpconf")      config.m_dumpconf      = true;
        else if (arg == "-m" || arg == "--monitor")       config.m_enableMonitor = true;
        else if (arg == "-D" || arg == "--dumpvars")      config.m_dumpvars      = true;
        else if (arg == "-n" || arg == "--do-nothing")    config.m_earlyquit     = true;
        else if (arg == "-w" || arg == "--allow-writes")  config.m_iowrite       = true;
        else if (arg == "-e" || arg == "--allow-escapes") config.m_openworld     = true;
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
        else if (arg[1] == 'L')  
        { 
            if (argv[++i] == NULL) {
                throw runtime_error("Error: expected filename");
            }
            string filename(argv[i]);
            char* endptr; 
            RegAddr  addr; 
            unsigned long index = strtoul(&arg[2], &endptr, 0); 
            if (*endptr != '\0') { 
                throw runtime_error("Error: invalid register specifier in option"); 
            } 
            addr = MAKE_REGADDR(RT_INTEGER, index);                      
            config.m_loads.push_back(make_pair(addr, filename)); 
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
    }

    if (config.m_programFile.empty())
    {
        throw runtime_error("Error: no program file specified");
    }

}



#ifdef ENABLE_COMA_ZL
void ConfigureCOMA(ProgramConfig& config, Config& configfile, LinkConfig& lkconfig) 
{
    // Get total number of cores
    lkconfig.m_nProcs = 0;
    const vector<PSize> placeSizes = configfile.getIntegerList<PSize>("NumProcessors");
    for (size_t i = 0; i < placeSizes.size(); ++i) 
        lkconfig.m_nProcs += placeSizes[i];

    // Get cache and directory configuration:
    lkconfig.m_nCachesPerDirectory = configfile.getInteger<size_t>("NumCachesPerDirectory", 8);
    lkconfig.m_nProcessorsPerCache = configfile.getInteger<size_t>("NumProcessorsPerCache", 4);
    lkconfig.m_nNumRootDirs        = configfile.getInteger<size_t>("NumRootDirectories", 4); 
    lkconfig.m_nMemoryChannels     = configfile.getInteger<size_t>("NumMemoryChannels", lkconfig.m_nNumRootDirs);

    lkconfig.m_nLineSize           = configfile.getInteger<size_t>("CacheLineSize", 64);

    // Cache properties:  
    lkconfig.m_nCacheAccessTime    = configfile.getInteger<size_t>("L2CacheDelay", 2);
    lkconfig.m_nCacheAssociativity = configfile.getInteger<size_t>("L2CacheAssociativity", 4);
    lkconfig.m_nCacheSet           = configfile.getInteger<size_t>("L2CacheNumSets", 128);
    lkconfig.m_nInject             = configfile.getBoolean("EnableCacheInjection", true);
    lkconfig.m_nCycleTimeCore      = 1000000 / configfile.getInteger<size_t>("CoreFreq",     1000); // ps per cycle
    lkconfig.m_nCycleTimeMemory    = 1000000 / configfile.getInteger<size_t>("DDRMemoryFreq", 800); // ps per cycle
}
#endif


Config* g_Config = NULL;

#ifdef ENABLE_COMA_ZL
int mgs_main(int argc, char const** argv)
#else    
# ifdef USE_SDL
    extern "C"
# endif
    int main(int argc, char** argv)
#endif
{
    srand(time(NULL));
    
    try
    {
        // Parse command line arguments
        ProgramConfig config;
#ifdef ENABLE_COMA_ZL
        ParseArguments(argc, (const char**)argv, config, LinkMGS::s_oLinkConfig);
#else
        ParseArguments(argc, (const char**)argv, config);
#endif

        if (config.m_interactive)
        {
            // Interactive mode
            PrintVersion(std::cout);
        }

        // Read configuration
        Config configfile(config.m_configFile, config.m_overrides);
        
        g_Config = &configfile;

        if (config.m_dumpconf)
        {
            std::clog << "### simulator version: " PACKAGE_VERSION << std::endl;
            configfile.dumpConfiguration(std::clog, config.m_configFile);
        }

#ifdef ENABLE_COMA_ZL
        ConfigureCOMA(config, configfile, LinkMGS::s_oLinkConfig);
        if (config.m_dumpconf)
            LinkMGS::s_oLinkConfig.dumpConfiguration(std::clog);
#endif

#ifdef ENABLE_COMA_ZL
        // finishing parsing config, now wait untile systemc topology is setup
        sem_post(&thpara.sem_sync);
        sem_wait(&thpara.sem_mgs);
        sem_post(&thpara.sem_sync);
#endif

        // Create the display
        Display display(configfile);

        // Create the system
        MGSystem sys(configfile, display, 
                     config.m_programFile, config.m_symtableFile, config.m_openworld, config.m_iowrite,
                     config.m_regs, config.m_loads, !config.m_interactive, !config.m_earlyquit);

#ifdef ENABLE_MONITOR
        string mo_mdfile = configfile.getString("MonitorMetadataFile", "mgtrace.md");
        string mo_tfile = configfile.getString("MonitorTraceFile", "mgtrace.out");
        Monitor mo(sys, config.m_enableMonitor, 
                   mo_mdfile, config.m_earlyquit ? "" : mo_tfile, !config.m_interactive);
#endif

        if (config.m_dumpvars)
        {
            std::cout << "### begin monitor variables" << std::endl;
            ListSampleVariables(std::cout);
            std::cout << "### end monitor variables" << std::endl;
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
            CommandLineReader clr(display);

            for (bool quit = false; !quit; )
            {
#ifdef ENABLE_MONITOR
                HandleCommandLine(clr, sys, mo, quit);
#else
                HandleCommandLine(clr, sys, quit);
#endif
            }
        }
    }
    catch (const exception& e)
    {
        PrintException(cerr, e);
        return 1;
    }
    
    return 0;
}
