#include "except.h"
#include "kernel.h"
#include <iostream>

using namespace std;

namespace Simulator
{

static string MakeMessage(const Object& object, const string& msg)
{
    return object.GetFQN() + ": " + msg;
}

SimulationException::SimulationException(const string& msg, const Object& object)
    : runtime_error(MakeMessage(object, msg))
{
    
}

SimulationException::SimulationException(const Object& object, const string& msg)
    : runtime_error(MakeMessage(object, msg))
{
    
}

void PrintException(ostream& out, const exception& e)
{
    out << endl << e.what() << endl;

    auto se = dynamic_cast<const SimulationException*>(&e);
    if (se != NULL)
    {
        // SimulationExceptions hold more information, print it
        for (auto& p : se->GetDetails())
            out << p << endl;
    }
}



}
