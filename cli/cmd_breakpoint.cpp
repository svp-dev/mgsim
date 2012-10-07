#include "commands.h"
#include <cstdlib>
#include <cerrno>

using namespace std;
using namespace Simulator;

bool cmd_bp_list(const vector<string>& /*command*/, vector<string>&/*args*/, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPointManager().ListBreakPoints(cout);
    return false;
}


bool cmd_bp_state(const vector<string>& /*command*/, vector<string>&/*args*/, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPointManager().ReportBreaks(cout);
    return false;
}


bool cmd_bp_on(const vector<string>& /*command*/, vector<string>&/*args*/, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPointManager().EnableCheck();
    return false;
}


bool cmd_bp_off(const vector<string>& /*command*/, vector<string>&/*args*/, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPointManager().DisableCheck();
    return false;
}


bool cmd_bp_clear(const vector<string>& /*command*/, vector<string>&/*args*/, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPointManager().ClearAllBreakPoints();
    return false;
}


bool cmd_bp_del(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    errno = 0;
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPointManager().DeleteBreakPoint(id);
    return false;
}


bool cmd_bp_enable(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    errno = 0;
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPointManager().EnableBreakPoint(id);
    return false;
}


bool cmd_bp_disable(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    errno = 0;
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPointManager().DisableBreakPoint(id);
    return false;
}


bool cmd_bp_add(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    int mode = 0;
    for (auto i : args[0])
    {
        switch(toupper(i))
        {
        case 'F': mode |= BreakPointManager::FETCH; break;
        case 'X': mode |= BreakPointManager::EXEC; break;
        case 'R': mode |= BreakPointManager::MEMREAD; break;
        case 'W': mode |= BreakPointManager::MEMWRITE; break;
        case 'T': mode |= BreakPointManager::TRACEONLY; break;
        default: break;
        }
    }
    if (mode == 0)
        cout << "Invalid breakpoint mode:" << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPointManager().AddBreakPoint(args[1], 0, mode);
    return false;
}


