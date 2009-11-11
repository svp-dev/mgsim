#ifndef _NETWORKBELOW_IF_H
#define _NETWORKBELOW_IF_H

#include "predef.h"

#include "network_if.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class NetworkBelow_if : public Network_if
{
public:

public:
	NetworkBelow_if(){
		m_bBelow = true;// avoid runtime judgment of class type
	}

};

//////////////////////////////
//} memory simulator namespace
}

#endif
