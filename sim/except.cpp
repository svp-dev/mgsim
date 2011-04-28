#include "except.h"
#include "kernel.h"
#include <algorithm>
#include <iostream>

namespace Simulator
{

static std::string MakeMessage(const Object& object, const std::string& msg)
{
    std::string name = object.GetFQN();
    std::transform(name.begin(), name.end(), name.begin(), toupper);
    return name + ": " + msg;
}

SimulationException::SimulationException(const std::string& msg, const Object& object)
    : std::runtime_error(MakeMessage(object, msg))
{
    
}

SimulationException::SimulationException(const Object& object, const std::string& msg)
    : std::runtime_error(MakeMessage(object, msg))
{
    
}

void PrintException(std::ostream& out, const std::exception& e)
{
    out << std::endl << e.what() << std::endl;

    const SimulationException* se = dynamic_cast<const SimulationException*>(&e);
    if (se != NULL)
    {
        // SimulationExceptions hold more information, print it
        const std::list<std::string>& details = se->GetDetails();
        for (std::list<std::string>::const_iterator p = details.begin(); p != details.end(); ++p)
        {
            out << *p << std::endl;
        }
    }
}



}
