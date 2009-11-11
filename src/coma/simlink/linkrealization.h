#ifndef _LINK_REALIZATION_H
#define  _LINK_REALIZATION_H

// this file has the global variable instances
// the file should be included for only once 

#include "linkmgs.h"
#include "th.h"

MemoryDataContainer* g_pMemoryDataContainer;
LinkMGS** g_pLinks;
LinkConfig LinkMGS::s_oLinkConfig;

th_t thpara;

#ifdef WIN32
unsigned int thread_proc(void* param);
#else
void* thread_proc(void*);
#endif

#endif

