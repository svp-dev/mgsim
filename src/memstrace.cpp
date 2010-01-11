#include "memstrace.h"
#include <iostream>
#include <iomanip>
#include "DCache.h"

using namespace std;

namespace Simulator{

ofstream g_osTraceFile;
size_t g_pid = 0xffff;
size_t g_tid = 0xffff;
ifstream g_isBatchFile;
ofstream g_osTraceFileDCache;
bool g_bEnableTraceDCache = false;
uint64_t g_u64TraceAddress;

void starttracing(const char* filename)
{
    char tempfilename[0xff];
    sprintf(tempfilename, "%s.dcache", filename);

    if (!g_osTraceFile.is_open())
    {
        g_osTraceFile.open(filename);
        if (g_osTraceFile.fail())
        {
            cout << "cannot open trace file." << endl;
            g_osTraceFile.close();
        }
    }

    if (!g_osTraceFileDCache.is_open())
    {
        g_osTraceFileDCache.open(tempfilename);
        if (g_osTraceFileDCache.fail())
        {
            cout << "cannot open trace dcache file." << endl;
            g_osTraceFileDCache.close();
        }
    }

#ifdef MEM_STORE_SEQUENCE_DEBUG
    sprintf(tempfilename, "%s.SSD", filename);
    startSSDebugtracing(tempfilename);
    sprintf(tempfilename, "%s.SSRD", filename);
    startSSRDebugtracing(tempfilename);
#endif
}

void stoptracing()
{
    if (g_osTraceFile.is_open())
        g_osTraceFile.close();

    if (g_osTraceFileDCache.is_open())
        g_osTraceFileDCache.close();

#ifdef MEM_STORE_SEQUENCE_DEBUG
    stopSSDebugtracing();
    stopSSRDebugtracing();
#endif
}

void tracepid(size_t pid)
{
    g_pid = pid;
}

void tracetid(size_t tid)
{
    g_tid = tid;
}

void traceproc(uint64_t cycleno, size_t pid, size_t tid, bool bwrite, uint64_t addr, size_t size, char* data)
{
    if ((pid == 0xffff)&&(tid == 0xffff))
        goto temp;

    if ((g_pid != 0xffff) && (pid != g_pid))
        return;

    if ((tid != 0xffff) && (tid != g_tid) && (g_pid == 0xffff) )
        return;

    if (!g_osTraceFile.is_open() && g_osTraceFile.good())
        return;

temp:

    g_osTraceFile << dec << cycleno << "\t" << pid << "\t" << ((tid == 0xffff)?(int)-1:(int)tid) << "\t" << bwrite << "\t" << hex << addr << "\t" << size << "#\t" ;

    if (data!=NULL)
    {
        for (unsigned int i=0;i<size;i++)
        {
            if ((i%16 == 0)&&(i!=0))
                g_osTraceFile << endl << "\t\t\t\t\t\t";
            
            g_osTraceFile << (unsigned int)(unsigned char)data[i] << " ";
        }
    }
   
    g_osTraceFile << endl;
}

void printtraceline(unsigned int state, uint64_t , unsigned int size, char* data)
{
    const char *pLineState[] = {
        "LINE_EMPTY",
        "LINE_LOADING",
        "LINE_PROCESSING",
        "LINE_INVALID",
        "LINE_FULL"
    };

    g_osTraceFileDCache << setw(12) << setfill(' ') << pLineState[state] << " ";
    for (unsigned int i=0;i<size;i++)
    {
        if ((i%16 == 0)&&(i!=0))
            g_osTraceFileDCache << endl << setw(26) << setfill(' ') << " ";
            
        g_osTraceFileDCache << hex << setw(2) << setfill('0') << (unsigned int)(unsigned char)data[i] << " ";
    }
    g_osTraceFileDCache << endl;
}

void settraceaddress(uint64_t addr)
{
    g_bEnableTraceDCache = true;
    g_u64TraceAddress = addr;
}

void tracecacheproc(Object* obj, uint64_t cycleno, size_t pid, unsigned int ext)
{
    if (!g_bEnableTraceDCache)
        return;

    //if ((pid != g_pid)&&(g_pid != 0xffff))
    //    return;

    if (!g_osTraceFileDCache.is_open() && g_osTraceFileDCache.good())
        return;

    DCache *pdcache = dynamic_cast<DCache*>(obj);

    size_t sets = pdcache->GetNumSets();
    size_t asso = pdcache->GetAssociativity();
    size_t linesize = pdcache->GetLineSize();
    MemAddr tag  = (g_u64TraceAddress / linesize) / sets;
    size_t  set  = (size_t)((g_u64TraceAddress / linesize) % sets) * asso;

    // Find the line
    for (size_t i = 0; i < asso; i++)
    {
        const DCache::Line *line = &(pdcache->GetLine(set + i));

        if (line->tag == tag)
        {
            // The wanted line was in the cache
            g_osTraceFileDCache << "#" << setw(6) << dec << cycleno << " @ " << pid << setw(2) << "  " << ext << setfill(' ') << " ";
            printtraceline(line->state, tag*linesize*sets, linesize, line->data);
            return;
        }
    
    }

}


}

