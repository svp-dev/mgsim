#ifndef LOG2_H
#define LOG2_H

#include <cmath>
#include "sys_config.h"

#ifndef HAVE_LOG2
extern "C" {
    static inline double log2(double x) { return log(x) / M_LN2; }
}
#endif

static inline unsigned int ilog2(unsigned long n) 
{
	unsigned r = 0;
	while (n > 1)
	{
		r++;
		n /= 2;
	}
	return r;
}

#endif
