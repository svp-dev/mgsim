#include "time.h"
#include <sys/time.h>

namespace Simulator
{

uint64_t GetTime()
{
    timeval time1; 
    gettimeofday(&time1, 0);
    return time1.tv_sec * 1000000 + time1.tv_usec; 
}

}
