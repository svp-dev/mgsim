// -*- c++-mode -*-
#ifndef CONVERTVAL_H
#define CONVERTVAL_H

#include <string>
#include <cctype>
#include <stdexcept>

/// ConvertToNumber<T>(string) - converts the given string to the numeric type T.
//
// A specialization is provided for bool that recognizes true/false
// and yes/no.
//
// Numeric SI suffixes (KB/KiB/MB/MiB/GB/GiB) provide a multiplier.
//
template<typename T>
struct converter {};

template<typename T>
T ConvertToNumber(const std::string& value)
{
    size_t idx;
    T val = converter<T>::convert(value, &idx);
    if (idx < value.size())
    {
        auto suffix = value.substr(idx);
        if (suffix == "KB") val *= 1000;
        else if (suffix == "KiB") val *= 1024;
        else if (suffix == "MB")  val = (val*1000)*1000;
        else if (suffix == "MiB") val = (val*1024)*1024;
        else if (suffix == "GB")  val = ((val*1000)*1000)*1000;
        else if (suffix == "GiB") val = ((val*1024)*1024)*1024;
        else throw std::invalid_argument("unknown suffix: " + suffix);
    }
    return val;
}

template<> struct converter<bool>
{ static bool convert(const std::string& s, size_t * /*unused*/); };

#define SPECIALIZE_ICONVERTER(Type, ConvFunc)                            \
    template<> struct converter<Type> {                                 \
        static Type convert(const std::string& s, size_t *i) { return std::ConvFunc(s, i, 0); } \
    }
#define SPECIALIZE_FCONVERTER(Type, ConvFunc)                           \
    template<> struct converter<Type> {                                 \
        static Type convert(const std::string& s, size_t *i) { return std::ConvFunc(s, i); } \
    }

SPECIALIZE_ICONVERTER(int, stoi);
SPECIALIZE_ICONVERTER(unsigned, stoul);
SPECIALIZE_ICONVERTER(long, stol);
SPECIALIZE_ICONVERTER(unsigned long, stoul);
SPECIALIZE_ICONVERTER(long long, stoll);
SPECIALIZE_ICONVERTER(unsigned long long, stoull);
SPECIALIZE_FCONVERTER(float, stof);
SPECIALIZE_FCONVERTER(double, stod);
SPECIALIZE_FCONVERTER(long double, stold);

#endif
