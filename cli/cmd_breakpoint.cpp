#include "commands.h"
#include <cstdlib>
#include <cerrno>

using namespace std;

bool cmd_bp_list(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().ListBreakPoints(cout);
    return false;
}


bool cmd_bp_state(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().ReportBreaks(cout);
    return false;
}


bool cmd_bp_on(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().EnableCheck();
    return false;
}


bool cmd_bp_off(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().DisableCheck();
    return false;
}


bool cmd_bp_clear(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    ctx.sys.GetKernel().GetBreakPoints().ClearAllBreakPoints();
    return false;
}


bool cmd_bp_del(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPoints().DeleteBreakPoint(id);
    return false;
}


bool cmd_bp_enable(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPoints().EnableBreakPoint(id);
    return false;
}


bool cmd_bp_disable(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    unsigned id = strtoul(args[0].c_str(), 0, 0);
    if (errno == EINVAL)
        cout << "Invalid breakpoint: " << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPoints().DisableBreakPoint(id);
    return false;
}


bool cmd_bp_add(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    int mode = 0;
    for (string::const_iterator i = args[0].begin(); i != args[0].end(); ++i)
    {
        switch(toupper(*i))
        {
        case 'R': mode |= BreakPoints::READ; break;
        case 'W': mode |= BreakPoints::WRITE; break;
        case 'X': mode |= BreakPoints::EXEC; break;
        case 'T': mode |= BreakPoints::TRACEONLY; break;
        default: break;
        }
    }
    if (mode == 0)
        cout << "Invalid breakpoint mode:" << args[0] << endl;
    else
        ctx.sys.GetKernel().GetBreakPoints().AddBreakPoint(args[1], 0, mode);
    return false;
}


