#include "commands.h"


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

