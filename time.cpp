#include "time.h"

namespace Simulator
{

#ifdef WIN32
// Windows version
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

uint64_t GetTime()
{
    static bool inited = false;
    static LARGE_INTEGER freq;
    if (!inited)
    {
        QueryPerformanceFrequency(&freq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart * 1000000 / freq.QuadPart;
}

#else
// Linux, Unix, MacOS version
#include <sys/time.h>

uint64_t GetTime()
{
    timeval time1; 
    gettimeofday(&time1, 0);
    return time1.tv_sec * 1000000 + time1.tv_usec; 
}
#endif
}

