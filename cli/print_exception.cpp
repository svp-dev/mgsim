#include "commands.h"
#include "sim/except.h"
#include <iostream>

using namespace std;

void PrintException(Simulator::MGSystem *sys, ostream& out, const exception& e)
{
    out << endl << e.what() << endl;

    auto se = dynamic_cast<const Simulator::SimulationException*>(&e);
    if (se != NULL)
    {
        // SimulationExceptions hold more information, print it
        for (auto& p : se->GetDetails())
            out << p << endl;

        auto te = dynamic_cast<const Simulator::ProgramTerminationException*>(se);
        if (te == NULL && se->GetPC() != 0)
        {
            // We avoid printing the backtrace when receiving an exception
            // for normal program termination.
            assert(sys != NULL);
            sys->Disassemble(se->GetPC());
        }
    }
}

