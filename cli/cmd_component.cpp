#include "commands.h"

using namespace Simulator;
using namespace std;

bool cmd_line(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    args.erase(args.begin());
    DoObjectCommand<Inspect::Line>(cout, ctx.sys, pat, args);
    return false;
}

bool cmd_info(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    args.erase(args.begin());
    DoObjectCommand<Inspect::Info>(cout, ctx.sys, pat, args);
    return false;
}

bool cmd_inspect(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    string pat = args[0];
    args.erase(args.begin());

    if (args.empty())
        ReadSampleVariables(cout, pat);
    DoObjectCommand<Inspect::Read>(cout, ctx.sys, pat, args);    
    return false;
}

