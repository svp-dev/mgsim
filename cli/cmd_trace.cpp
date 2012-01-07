#include "commands.h"

using namespace Simulator;
using namespace std;

bool cmd_trace_show(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    string debugStr;
    int m = ctx.sys.GetDebugMode();
    if (m & Kernel::DEBUG_PROG)     debugStr += " prog";
    if (m & Kernel::DEBUG_SIM)      debugStr += " sim";
    if (m & Kernel::DEBUG_DEADLOCK) debugStr += " deadlocks";
    if (m & Kernel::DEBUG_FLOW)     debugStr += " flow";
    if (m & Kernel::DEBUG_MEM)      debugStr += " mem";
    if (m & Kernel::DEBUG_IO)       debugStr += " io";
    if (m & Kernel::DEBUG_REG)      debugStr += " regs";
    if (m & Kernel::DEBUG_NET)      debugStr += " net";
    if (m & Kernel::DEBUG_IONET)    debugStr += " ionet";
    if (m & Kernel::DEBUG_FPU)      debugStr += " fpu";
    if (!debugStr.size()) debugStr = " (nothing)";
    cout << "Tracing enabled for:" << debugStr << endl;
    return false;
}

bool cmd_trace_debug(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    for (size_t i = 0; i < args.size(); ++i)
    {
        string tcmd = args[i];
        
        if      (tcmd == "sim")       ctx.sys.ToggleDebugMode(Kernel::DEBUG_SIM);
        else if (tcmd == "prog")      ctx.sys.ToggleDebugMode(Kernel::DEBUG_PROG);
        else if (tcmd == "deadlocks") ctx.sys.ToggleDebugMode(Kernel::DEBUG_DEADLOCK);
        else if (tcmd == "flow")      ctx.sys.ToggleDebugMode(Kernel::DEBUG_FLOW);
        else if (tcmd == "mem")       ctx.sys.ToggleDebugMode(Kernel::DEBUG_MEM);
        else if (tcmd == "io")        ctx.sys.ToggleDebugMode(Kernel::DEBUG_IO);
        else if (tcmd == "regs")      ctx.sys.ToggleDebugMode(Kernel::DEBUG_REG);
        else if (tcmd == "net")       ctx.sys.ToggleDebugMode(Kernel::DEBUG_NET);
        else if (tcmd == "ionet")     ctx.sys.ToggleDebugMode(Kernel::DEBUG_IONET);
        else if (tcmd == "fpu")       ctx.sys.ToggleDebugMode(Kernel::DEBUG_FPU);
        else if (tcmd == "all")       ctx.sys.SetDebugMode(-1);
        else if (tcmd == "none")      ctx.sys.SetDebugMode(0);
    }
    return cmd_trace_show(command, args, ctx);
}

bool cmd_trace_line(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    args.erase(args.begin());
    DoObjectCommand<Inspect::Trace>(cout, ctx.sys, pat, args);
    return false;
}

