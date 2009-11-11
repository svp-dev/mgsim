#ifndef _BUS_ARBITOR_H
#define _BUS_ARBITOR_H

#include "predef.h"

#include "busst_arbiter_if.h"

namespace MemSim{
//{ memory simulator namespace

class BusST_Arbiter : public BusST_Arbiter_if
{
public:
	// constructor
	BusST_Arbiter(sc_module_name name) : sc_module(name)
	{

	}


};

//} memory simulator namespace
}
#endif
