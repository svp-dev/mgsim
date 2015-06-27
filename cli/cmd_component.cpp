#include "commands.h"
#include <sim/sampling.h>

using namespace Simulator;
using namespace std;

bool cmd_line(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    args.erase(args.begin());
    DoObjectCommand<Inspect::Line>(cout, ctx.sys, pat, args);
    return false;
}

bool cmd_info(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    args.erase(args.begin());
    DoObjectCommand<Inspect::Info>(cout, ctx.sys, pat, args);
    return false;
}

bool cmd_inspect(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    args.erase(args.begin());

    if (args.empty())
    {
        bool match = ctx.sys.GetKernel()->GetVariableRegistry().RenderVariables(cout, pat, false);
        if (!match)
            cout << "No variables found." << endl;
    }
    DoObjectCommand<Inspect::Read>(cout, ctx.sys, pat, args);
    return false;
}

bool cmd_dump(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    bool match = ctx.sys.GetKernel()->GetVariableRegistry().RenderVariables(cout, pat, true);
    if (!match)
        cout << "No variables found." << endl;
    return false;
}

bool cmd_write(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    args.erase(args.begin());
    DoObjectCommand<Inspect::Write>(cout, ctx.sys, pat, args);
    return false;
}

bool cmd_set(const vector<string>& /*command*/, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    string val = args[1];
    for (size_t i = 2; i < args.size(); ++i)
        val = val + ' ' + args[i];
    try
    {
        ctx.sys.GetKernel()->GetVariableRegistry().SetVariables(cout, pat, val);
    }
    catch (const exception& e)
    {
        PrintException(&ctx.sys, cerr, e);
    }
    return false;
}
