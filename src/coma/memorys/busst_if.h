#ifndef _BUSST_IF_H
#define _BUSST_IF_H

#include "predef.h"

namespace MemSim{
//{ memory simulator namespace

// master interface
class BusST_if 
	: public virtual sc_interface, public virtual SimObj
{
public:
    virtual bool request(ST_request*) = 0;

	virtual void MasterNotify() = 0;
	virtual void SlaveNotify() = 0;

}; 

//} memory simulator namespace
}
#endif

