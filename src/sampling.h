#ifndef SAMPLING_H
#define SAMPLING_H

#include <string>
#include <iostream>
#include <cassert>

enum SampleVariableDataType {
    SV_INTEGER,
    SV_FLOAT
};

enum SampleVariableCategory {
    SVC_LEVEL,    // current level: cycle counter, #threads, etc 
    SVC_STATE,    // state variable
    SVC_WATERMARK,  // mins and maxs, evolves monotonously
    SVC_CUMULATIVE, // integral of level over time
};

template<typename T> struct _sv_detect_type { static const SampleVariableDataType type = SV_INTEGER; };
template<> struct _sv_detect_type<float> { static const SampleVariableDataType type = SV_FLOAT; };
template<> struct _sv_detect_type<double> { static const SampleVariableDataType type = SV_FLOAT; };

template<typename T>
void RegisterSampleVariable(T& var, const std::string& name, SampleVariableCategory cat, T max = (T)0)
{
    extern void _RegisterSampleVariable(void*, size_t, const std::string&, SampleVariableDataType, SampleVariableCategory, void*);
    _RegisterSampleVariable(&var, sizeof(T), name, _sv_detect_type<T>::type, cat, &max);
}

#define RegisterSampleVariableInObject(var, cat, ...)                       \
    RegisterSampleVariable(var, GetFQN() + '.' + (#var + 2) , cat, ##__VA_ARGS__)

#define RegisterSampleVariableInObjectWithName(var, name, cat, ...)      \
    RegisterSampleVariable(var, GetFQN() + '.' + name, cat, ##__VA_ARGS__)

void ListSampleVariables(std::ostream& os, const std::string &pat = "*");
void ShowSampleVariables(std::ostream& os, const std::string &pat = "*");

#endif
