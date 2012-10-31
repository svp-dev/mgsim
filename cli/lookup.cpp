#include "commands.h"
#include <sstream>

using namespace std;

bool DoCommand(vector<string>& args, cli_context& ctx)
{
    bool match = false;
    bool quit = false;
    stringstream backup;
    backup.copyfmt(cout);

    for (const command_descriptor *p = &command_table[0]; p->prefix[0] != 0; ++p)
    {
        // First check that the start of the command matches.

        size_t i;
        match = true;
        for (i = 0; p->prefix[i] != 0; ++i)
        {
            if (i >= args.size() || args[i] != p->prefix[i])
            {
                match = false;
                break;
            }
        }

        if (!match)
            continue;

        // We have a match. Check the number of arguments.
        // here i points to the first non-command argument
        size_t n_remaining_args = args.size() - i;
        if ((int)n_remaining_args < p->min_args)
            continue;
        if (p->max_args >= 0 && (int)n_remaining_args > p->max_args)
            continue;

        // We have a match for both the command and the arguments.
        // Get the command separately.
        vector<string> command(args.begin(), args.begin() + i);
        args.erase(args.begin(), args.begin() + i);

        quit = p->handler(command, args, ctx);
        cout << endl;
        break;
    }
    cout.copyfmt(backup);

    if (!match)
        cout << "Unknown command." << endl;
    return quit;
}


