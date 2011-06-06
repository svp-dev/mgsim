#include "commands.h"
#include <iomanip>

using namespace std;

void ExpandAliases(vector<string>& args)
{
    string command = args[0];
    args.erase(args.begin());

    for (const command_alias *p = &alias_table[0]; p->alias != 0; ++p)
    {
        if (p->alias == command)
        {
            size_t i;
            for (i = 0; p->subst[i] != 0; ++i) ;
            args.insert(args.begin(), &p->subst[0], &p->subst[i]);
            return;
        }
    }

    // No alias found, insert command back at beginning.
    args.insert(args.begin(), command);
}

bool cmd_aliases(const vector<string>& command, vector<string>& args, cli_context& ctx)
{
    cout << "Aliases:" << endl;
    for (const command_alias *p = &alias_table[0]; p->alias != 0; ++p)
    {
        cout << setw(10) << p->alias;
        for (size_t i = 0; p->subst[i] != 0; ++i)
            cout << ' ' << p->subst[i];
        cout << endl;
    }
    cout << endl;
    return false;
}


