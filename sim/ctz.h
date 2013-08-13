#ifndef CTZ_H
#define CTZ_H

// Returns the number of trailing 0-bits in x, starting at the least
// significant bit position. (If x is 0, the result is undefined.)

#ifdef __GNUC__
# define ctz(N) __builtin_ctz(N)
#else
template<typename T>
inline int ctz(T x)
{
    int p, b;
    for (p = 0, b = 1; !(b & x); b <<= 1, ++p)
        ;
    return p;
}
#endif

#endif
