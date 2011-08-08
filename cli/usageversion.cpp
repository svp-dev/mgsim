#include "commands.h"


void PrintVersion(std::ostream& out)
{
    out <<
        "mgsim (Microgrid Simulator) " PACKAGE_VERSION "\n"
        "Copyright (C) 2008,2009,2010,2011 Universiteit van Amsterdam.\n"
        "\n"
        "Written by Mike Lankamp. Contributions by Li Zhang, Raphael 'kena' Poss.\n";
}

void PrintUsage(std::ostream& out, const char* cmd)
{
    out <<
        "Microgrid Simulator.\n\n"
        "Usage: " << cmd << " [ARG]...\n\n"
        "Options:\n\n"
        "  -a, --area VAL               Dump area information, assume technology is VAL nm.\n"
        "  -c, --config FILE            Read configuration from FILE.\n"
        "  -d, --dump-configuration     Dump configuration to standard error prior to program startup.\n"
        "  -i, --interactive            Start the simulator in interactive mode.\n"
        "  -l, --list-mvars             Dump list of monitor variables prior to program startup.\n"
#ifdef ENABLE_MONITOR
        "  -m, --monitor                Enable simulation monitoring.\n"
#endif
        "  -n, --do-nothing             Exit before the program starts, but after the system is configured.\n"
        "  -o, --override NAME=VAL      Overrides the configuration option NAME with value VAL.\n"
        "  -p, --print-final-mvars PAT  Print the value of all monitoring variables matching PAT.\n"
        "  -q, --quiet                  Do not print simulation statistics after execution.\n"
        "  -s, --symtable FILE          Read symbol from FILE. (generate with nm -P)\n"
        "  -t, --terminate              Terminate the simulator upon an exception.\n"
        "  -T, --dump-topology FILE     Dump the grid topology to FILE prior to program startup.\n"
        "  --no-node-properties         Do not print component properties in the topology output.\n"
        "  --no-edge-properties         Do not print link properties in the topology output.\n"
        "  -R<X> VALUE                  Store the integer VALUE in the specified register.\n"
        "  -F<X> VALUE                  Store the float VALUE in the specified FP register.\n"
        "  -L<X> FILE                   Create an ActiveROM component with the contents of FILE\n"
        "                               and store the address in the specified register.\n"
        "Other options:\n"
        "  -h, --help                   Print this help, then exit.\n"
        "      --version                Print version information, then exit.\n"
        "\n"
        "Report bugs and suggestions to " PACKAGE_BUGREPORT ".\n";
}

