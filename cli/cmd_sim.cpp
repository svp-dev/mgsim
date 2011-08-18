#include "commands.h"
#include <cerrno>

using namespace Simulator;
using namespace std;

bool cmd_run(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    // Step or run

    CycleNo nCycles = INFINITE_CYCLES;
    if (command[0][0] == 's')
    {
        // Step
        nCycles = args.empty() ? 1 : max(1UL, strtoul(args[0].c_str(), 0, 0));
    }

    // Flush the history, in case something
    // bad happens in StepSystem...
    ctx.clr.CheckPointHistory();

#ifdef ENABLE_MONITOR
    ctx.mon.start();
#endif
    try
    {
        StepSystem(ctx.sys, nCycles);
    }
    catch (const exception& e)
    {
        PrintException(cerr, e);
    }
#ifdef ENABLE_MONITOR
    ctx.mon.stop();
#endif
    return false;
}

bool cmd_state(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.PrintState(args);
    return false;
}

bool cmd_lookup(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    errno = 0;
    MemAddr addr = strtoull(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid address: " << args[0] << endl;
    else
        cout << ctx.sys.GetSymTable()[addr] << endl;
    return false;
}


bool cmd_disas(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    MemAddr addr = 0;

    errno = 0;
    addr = strtoull(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
    {
        bool check = ctx.sys.GetKernel().GetSymbolTable().LookUp(args[0], addr, true);
        if (!check)
        {
            cout << "invalid address: " << args[0] << endl;
            return false;
        }
    }

    size_t sz = 0;
    if (args.size() > 1)
        sz = strtoul(args[1].c_str(), 0, 0);
    if (sz)
        ctx.sys.Disassemble(addr, sz);
    else
        ctx.sys.Disassemble(addr);
    return false;
}

bool cmd_stats(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.PrintAllStatistics(cout);
    return false;
}

