#ifndef _NETWORKABOVE_IF_H
#define _NETWORKABOVE_IF_H

#include "predef.h"

#include "network_if.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class NetworkAbove_if : public Network_if
{
public:

public:
	NetworkAbove_if(){
		m_bBelow = false;// avoid runtime judgment of class type
	}

};

//////////////////////////////
//} memory simulator namespace
}

#endif
