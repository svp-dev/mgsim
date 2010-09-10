#include "predef.h"
#include <string>
#include "inttypes.h"
using namespace std;

#include "string.h"

#include "../simlink/linkmgs.h"
#include "directorytok.h"

namespace MemSim
{

unsigned int ST_request::s_nRequestAlignedSize;

unsigned int g_nCacheLineSize  = 0;

int lg2(int n)
{
	int r = 0;
	while (n > 1)
	{
		r++;
		n /= 2;
	}
	return r;
}

unsigned int lg2(unsigned int n)
{
	int r = 0;
	while (n > 1)
	{
		r++;
		n /= 2;
	}
	return r;
}

void ST_request::Conform2BitVecFormat()
{
    // initialize bit vector
    std::fill(bitmask, bitmask + CACHE_BIT_MASK_WIDTH, false);

    // alignment is required 
    assert(nsize != 0);

    for (unsigned int i = 0; i < g_nCacheLineSize; i++)
    {
        if (i >= offset && i < offset + nsize)
        {
            bitmask[i] = true;
        }
    }
}

void ST_request::Conform2SizeFormat()
{
    unsigned int validsize = 0;
    unsigned int validoffset = 0;
    bool bsegstart = false;
    bool bsegend   = false;

    // 00 -> 10 -> 01 -> 01 | error
    // check whether the mask is valid to transform
    for (unsigned int i = 0; i < g_nCacheLineSize; i++)
    {
        if (bitmask[i])
        {
	        assert(!bsegend);
            if (!bsegstart)
            {
                validoffset = i;
                bsegstart = true;
            }
        }
        else if (!bsegend)
        {
            if (bsegstart)
            {
                bsegend   = true;
                validsize = i - validoffset;
                bsegstart = false;
            }
        }
    }

    nsize  = validsize;
    offset = validoffset;
}

unsigned int CacheState::s_nTotalToken = 0;

bool ST_request::IsRequestWithCompleteData()
{
    return (type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA || type == MemoryState::REQUEST_DISSEMINATE_TOKEN_DATA) && (tokenacquired > 0);
}

bool ST_request::IsRequestWithNoData()
{
    if (IsRequestWithCompleteData())
        return false;

    if ( (type == MemoryState::REQUEST_WRITE) || (type == MemoryState::REQUEST_ACQUIRE_TOKEN) || ((type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA)&&( (tokenacquired > 0)||(tokenrequested == CacheState::GetTotalTokenNum()) ) ) || (type == MemoryState::REQUEST_READ_REPLY) )
        return false;

    return true;
}

bool ST_request::IsRequestWithModifiedData()
{
    if ( (type == MemoryState::REQUEST_ACQUIRE_TOKEN) || ((type == MemoryState::REQUEST_ACQUIRE_TOKEN_DATA)&&(tokenrequested == CacheState::GetTotalTokenNum())) || ((type == MemoryState::REQUEST_WRITE_REPLY)&&(bmerged)) )
        return true;

    return false;
}

bool cache_line_t::IsLineAtCompleteState()
{
    if (state == CacheState::CLS_INVALID)
        return false;

    if (state == CacheState::CLS_SHARER && pending)
    {
        return !contains(bitmask, bitmask + CACHE_BIT_MASK_WIDTH, false);
    }
    
    return (state != CacheState::CLS_OWNER || tokencount != 0);
}

}




