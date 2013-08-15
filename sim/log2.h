#ifndef LOG2_H
#define LOG2_H

template <typename T>
inline constexpr bool IsPowerOfTwo(T x)
{
    return (x & (x - 1)) == 0;
}

template<typename T>
#ifdef __GNUC__
__attribute__((const))
#endif
inline unsigned ilog2(T n)
{
    // returns the first power of two equal to
    // or greater than n. For example ilog2(1) = 0, ilog2(4) = 2, ilog2(5) = 3
    // Note that this is different from "find position of leftmost bit".
    unsigned l = 0;
    T r = 1;
    while (r < n)
    {
        l++;
        r *= 2;
    }
    return l;
}

#endif
