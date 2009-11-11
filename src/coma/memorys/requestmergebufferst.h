#ifndef _REQUESTMERGEBUFFER_H
#define _REQUESTMERGEBUFFER_H

#include "predef.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class RequestMergeBuffer
{
private:
	sc_fifo<ST_request*> m_outputFifo;

	// constructor
	RequestMergeBuffer(){};

	write(ST_request* req)
	{

	}

	int num_available_fast(){}

	ST_request* read(){}
};

//////////////////////////////
//} memory simulator namespace
}

#endif
