#include "simcontrol.h"
using namespace MemSim;

namespace MemSim{
streambuf* SimObj::logbufGlobal = NULL;
ofstream* SimObj::ofsGlobal = NULL;
streambuf* SimObj::clogbufDefault = clog.rdbuf();
vector<SimObj*> SimObj::s_vecObj;
}

