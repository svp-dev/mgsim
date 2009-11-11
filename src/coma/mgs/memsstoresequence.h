#ifndef MEMSSTORESEQUENCE_H
#define MEMSSTORESEQUENCE_H

#include <cstdlib>
#include <fstream>
#include <cstddef>
#include <stdint.h>

using namespace std;

namespace Simulator
{
    typedef struct __storeentryheader_t{
        uint64_t            cycleno;
        unsigned int        pid;
        uint64_t            address;
        unsigned int        size;
        uint64_t            cycleno_end;
    } storeentryheader_t;


    extern ofstream g_osSSDTraceFile;
    extern ofstream g_osSSRDTraceFile;

    void startSSRDebugtracing(char* filename);
    void startSSDebugtracing(char* filename);
    void stopSSRDebugtracing();
    void stopSSDebugtracing();
    // debug and trace store seqence 
    void debugSSproc(uint64_t cycleno, unsigned int pid, uint64_t address, unsigned int size, char* data);

    // debug and trace store sequence upon their return
    void debugSSRproc(uint64_t cycleno_start, uint64_t cycleno_end, unsigned int pid, uint64_t address, unsigned int size, char* data);
}


#endif
