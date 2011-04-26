#include "except.h"
#include "kernel.h"
#include <algorithm>

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

}
