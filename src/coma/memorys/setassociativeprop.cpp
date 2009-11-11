#include "setassociativeprop.h"
using namespace MemSim;

namespace MemSim{
// static - cachelinesize and bitwidth
unsigned int SetAssociativeProp::s_nLineSize = 0;
unsigned int SetAssociativeProp::s_nLineBits = 0;

// global - cachelinesize
unsigned int &g_nCacheLineWidth = SetAssociativeProp::s_nLineBits;
unsigned int &g_nCacheLineSize = SetAssociativeProp::s_nLineSize;
}

