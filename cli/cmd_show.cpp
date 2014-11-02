#include "commands.h"
#include <sim/sampling.h>
#include <arch/dev/IODeviceDatabase.h>

using namespace Simulator;
using namespace std;


bool cmd_show_vars(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = "*";
    if (!args.empty())
        pat = args[0];
    ctx.sys.GetKernel()->GetVariableRegistry().ListVariables(cout, pat);
    return false;
}


bool cmd_show_syms(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = "*";
    if (!args.empty())
        pat = args[0];
    ctx.sys.GetSymTable().Write(cout, pat);
    return false;
}


bool cmd_show_components(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = "*";
    if (!args.empty())
        pat = args[0];

    size_t levels = 0;
    if (args.size() > 1)
        levels = strtoul(args[1].c_str(), 0, 0);

    ctx.sys.PrintComponents(cout, pat, levels);
    return false;
}

bool cmd_show_processes(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = "*";
    if (!args.empty())
        pat = args[0];
    ctx.sys.PrintProcesses(cout, pat);
    return false;
}


bool cmd_show_devdb(const vector<string>& /*command*/, vector<string>& /*args*/, cli_context& /*ctx*/)
{
    DeviceDatabase::GetDatabase().Print(std::cout);
    return false;
}

