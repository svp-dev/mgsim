#include "memsstoresequence.h"
#include <iostream>
using namespace std;

namespace Simulator
{
    ofstream g_osSSDTraceFile;
    ofstream g_osSSRDTraceFile;

    void startSSDebugtracing(char* filename)
    {
        if (!g_osSSDTraceFile.is_open())
        {
            g_osSSDTraceFile.open(filename, ofstream::binary);
            if (g_osSSDTraceFile.fail())
            {
                cout << "cannot open store sequence debug trace file." << endl;
                g_osSSDTraceFile.close();
            }
        }
    }

    void startSSRDebugtracing(char* filename)
    {
        if (!g_osSSRDTraceFile.is_open())
        {
            g_osSSRDTraceFile.open(filename, ofstream::binary);
            if (g_osSSRDTraceFile.fail())
            {
                cout << "cannot open store sequence debug trace file." << endl;
                g_osSSRDTraceFile.close();
            }
        }
    }

    void stopSSDebugtracing()
    {
        if (g_osSSDTraceFile.is_open())
            g_osSSDTraceFile.close();
    }

    void stopSSRDebugtracing()
    {
        if (g_osSSRDTraceFile.is_open())
            g_osSSRDTraceFile.close();
    }

    void debugSSproc(uint64_t cycleno, unsigned int pid, uint64_t address, unsigned int size, char* data)
    {
//        cout << dec << cycleno << "\t" << pid << "\t" << hex << address << "\t" << size << endl;
      storeentryheader_t header = {cycleno, pid, address, size, 0};
        g_osSSDTraceFile.write((const char*)&header, sizeof(storeentryheader_t));

        g_osSSDTraceFile.write((const char*)data, size);
    }

    void debugSSRproc(uint64_t cycleno_start, uint64_t cycleno_end, unsigned int pid, uint64_t address, unsigned int size, char* data)
    {
//        cout << dec << cycleno << "\t" << pid << "\t" << hex << address << "\t" << size << endl;
        storeentryheader_t header = {cycleno_start, pid, address, size, cycleno_end};
        g_osSSRDTraceFile.write((const char*)&header, sizeof(storeentryheader_t));

        g_osSSRDTraceFile.write((const char*)data, size);
    }

}
