#ifndef RANGE_H
#define RANGE_H

#include <set>
#include <string>
#include <sstream>

template <typename T>
std::set<T> parse_range(const std::string& str, const T& first, const T& end)
{
    // Read the range
    std::set<T> elems;
    std::stringstream is(str);
    std::string s;
    while (std::getline(is, s, ',')) {
        if (s == "all") {
            for (T i = first; i != end; ++i) {
                elems.insert(i);
            }
        } else {
            std::stringstream is2(s);
            T a(end), last(end); --last;
            is2 >> a;
            if (a >= first && a < end) {
                T b(a);
                if (is2.get() == '-') {
                    is2 >> b;
                    b = std::min<T>(b, last);
                }
                b = std::max<T>(a,b);
                for (T i = a; i <= b; ++i) {
                    elems.insert(i);
                }
            }
        }
    }
    return elems;
}

#endif
