#include "kernel.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
using namespace std;

namespace Simulator
{

static string ConstructErrorMessage(const Object& obj, const string& message)
{
    string name = obj.GetFQN();
    transform(name.begin(), name.end(), name.begin(), ::toupper);
    
    stringstream ss;
    ss << "[" << setw(8) << setfill('0') << obj.GetKernel()->GetCycleNo() << ":" << name << "] " << message;
    return ss.str();
}

SimulationException::SimulationException(const Object& obj, const string& message)
    : runtime_error(ConstructErrorMessage(obj, message))
{
}

}
