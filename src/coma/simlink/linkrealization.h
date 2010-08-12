#ifndef _LINK_REALIZATION_H
#define  _LINK_REALIZATION_H

// this file has the global variable instances
// the file should be included for only once 

#include "linkmgs.h"
#include "th.h"

Simulator::VirtualMemory* g_pMemoryDataContainer;
LinkConfig LinkMGS::s_oLinkConfig;

th_t thpara;

void* thread_proc(void*);

#endif

