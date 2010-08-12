#ifndef LOG2_H
#define LOG2_H

static inline unsigned int ilog2(unsigned long n) 
{
    unsigned l = 0;
    if (n > 0)
    {
        unsigned r = 1;
        while (r < n)
        {
            l++;
            r *= 2;
        }
    }
    return l;
}

#endif
