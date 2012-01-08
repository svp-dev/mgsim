#include "commands.h"
#include <iomanip>

using namespace std;

bool cmd_help(const vector<string>& /*command*/, vector<string>& /*args*/, cli_context& /*ctx*/)
{
    cout << left;
    for (const command_descriptor *p = &command_table[0]; p->prefix[0] != 0; ++p)
    {
        cout << setw(35) << setfill(' ') << p->use_format << " " << p->short_help << endl;
    }
    return false;
}


bool cmd_quit(const vector<string>& /*command*/, vector<string>& /*args*/, cli_context& /*ctx*/)
{
    cout << "Thank you. Come again!" << endl;
    return true;
}



