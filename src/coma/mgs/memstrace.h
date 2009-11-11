#ifndef MEMSTRACE_H
#define MEMSTRACE_H

#include <cstdlib>
#include <fstream>
#include <cstddef>

using namespace std;

#include "VirtualMemory.h"
#include "kernel.h"
#include "../memorys/dcswitch.h"

#ifdef MEM_STORE_SEQUENCE_DEBUG
#include "memsstoresequence.h"
#endif

namespace Simulator
{

extern ofstream g_osTraceFile;
extern size_t g_pid;
extern size_t g_tid;
extern ifstream g_isBatchFile;
extern ofstream g_osTraceFileDCache;
extern bool g_bEnableTraceDCache;
extern uint64_t g_u64TraceAddress;


void starttracing(const char* filename);
void stoptracing();
void tracepid(size_t pid);
void tracetid(size_t tid);
void traceproc(uint64_t cycleno, size_t pid, size_t tid, bool bwrite, uint64_t addr, size_t size, char* data);
void settraceaddress(uint64_t addr);
void tracecacheproc(Object* obj, uint64_t cycleno, size_t pid, unsigned int ext=0);

}

#endif
