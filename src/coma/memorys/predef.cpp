#include "predef.h"

namespace MemSim
{

unsigned int Message::s_nRequestAlignedSize;

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

unsigned int CacheState::s_nTotalToken = 0;

}
