#ifndef COMMANDS_H
#define COMMANDS_H

#include "simreadline.h"

#ifdef ENABLE_MONITOR
# include <sim/monitor.h>
#endif
#include <arch/MGSystem.h>

#include <vector>
#include <string>

struct cli_context
{
    CommandLineReader& clr;
    Simulator::MGSystem& sys;
#ifdef ENABLE_MONITOR
    Monitor& mon;
#endif        
};

// handler for a command in interactive mode.
// should return "false" if the simulation is to be stopped.
typedef bool  (command_handler)(const std::vector<std::string>& command, std::vector<std::string>& args, cli_context& ctx);



struct command_descriptor
{
    const char         *prefix[3];
    int                min_args;
    int                max_args;
    command_handler    *handler;
    const char         *use_format;
    const char         *short_help;
};

extern const command_descriptor command_table[];

struct command_alias
{
    const char *alias;
    const char *subst[4];
};

extern const command_alias alias_table[];

void ExpandAliases(std::vector<std::string>& command);

// the following should return true to exit the simulation
bool DoCommand(std::vector<std::string>& command, cli_context& ctx);
bool HandleCommandLine(cli_context& ctx); 


// Some public commands

void StepSystem(Simulator::MGSystem& system, Simulator::CycleNo cycles);

template<unsigned Type>
void DoObjectCommand(std::ostream& out, Simulator::MGSystem& sys, const std::string& pat, std::vector<std::string>& args)
{
    Simulator::MGSystem::object_map_t objs = sys.GetComponents(pat);
    if (objs.empty())
    {
        out << "No components found." << std::endl;
        return;
    }

    bool some = false;
    for (Simulator::MGSystem::object_map_t::const_iterator i = objs.begin(); i != objs.end(); ++i)
    {
        Inspect::Interface_<Type> * interface = dynamic_cast<Inspect::Interface_<Type>*>(i->second);
        if (interface != NULL)
        {
            if (some)
                out << std::endl;
            out << i->first << ':' << std::endl;

            interface->DoCommand(out, args);

            if (Type == Inspect::Info)
            {
                out << std::endl << "Supported commands: ";
                Inspect::ListCommands *lc = dynamic_cast<Inspect::ListCommands*>(i->second);
                assert(lc != NULL);
                lc->ListSupportedCommands(out);
            }

            some = true;
        }
    }
    if (!some)
        out << "No components found that support this command." << std::endl;
}


#endif
