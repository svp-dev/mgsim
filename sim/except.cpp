#include "except.h"
#include "kernel.h"
#include <iostream>

using namespace std;

namespace Simulator
{

static string MakeMessage(const Object& object, const string& msg)
{
    return object.GetName() + ": " + msg;
}

SimulationException::SimulationException(const string& msg, const Object& object)
    : runtime_error(MakeMessage(object, msg)), m_details(), m_pc(0)
{

}

SimulationException::SimulationException(const Object& object, const string& msg)
    : runtime_error(MakeMessage(object, msg)), m_details(), m_pc(0)
{

}

}
