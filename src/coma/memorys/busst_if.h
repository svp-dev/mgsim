#ifndef _BUSST_IF_H
#define _BUSST_IF_H

#include "predef.h"

namespace MemSim
{

class BusST_if : public virtual sc_interface
{
public:
    virtual bool request(ST_request*) = 0;
}; 

}
#endif

