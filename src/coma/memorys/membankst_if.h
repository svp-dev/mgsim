#ifndef _MEMBANKST_IF_H
#define _MEMBANKST_IF_H

#include "predef.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class MemBank_if : public sc_interface
{

public:
	sc_event m_eFeedbackEvent;

	// Slave interface
	/*virtual bool request(ST_request *req) = 0;*/
	//	virtual bool read(ST_request *req) = 0;
	//	virtual bool write(ST_request *req) = 0;
};

//////////////////////////////
//} memory simulator namespace
}

#endif
