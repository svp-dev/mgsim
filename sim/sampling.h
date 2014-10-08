// -*- c++ -*-
#ifndef SAMPLING_H
#define SAMPLING_H

#include <type_traits>
#include <vector>
#include <string>

class Config;

namespace Simulator
{
    enum SampleVariableDataType {
        SV_BOOL,    // boolean
        SV_INTEGER, // fixed-width integer value
        SV_FLOAT,   // fixed-width floating-point
        SV_BINARY,  // fixed-width binary (char array)
        SV_CUSTOM   // custom I/O primitives
    };

    enum SampleVariableCategory {
        SVC_STATE,      // state variable
        SVC_LEVEL,      // stats: current level: cycle counter, #threads, etc
        SVC_WATERMARK,  // stats: mins and maxs, evolves monotonously
        SVC_CUMULATIVE, // stats: integral of level over time
    };

    /// RegisterSampleVariable: perform a registration.
    void RegisterSampleVariable(void* ptr, const std::string& name,
                                SampleVariableCategory cat,
                                SampleVariableDataType t,
                                size_t s, void* ref);

    // c++ misses is_bool, so define it here.
    template <typename T>
    using is_bool = std::is_same<typename std::remove_cv<T>::type, bool>;

    template<typename T>
    void RegisterSampleVariable(T& var, const std::string& name, SampleVariableCategory cat)
    {
        static_assert(std::is_arithmetic<T>::value == true
                      || std::is_enum<T>::value, "This form of RegisterSampleVariable works only on scalars, bools or enums");

        SampleVariableDataType t = \
            std::is_floating_point<T>::value ? SV_FLOAT :
            is_bool<T>::value ? SV_BOOL :
            SV_INTEGER;

        RegisterSampleVariable(&var, name, cat, t, sizeof(T), 0);
    }

    template<typename T>
    inline
    void RegisterSampleVariable(std::vector<T>& var, const std::string& name, SampleVariableCategory cat)
    {
        RegisterSampleVariable(&var[0], name, cat, SV_BINARY, var.size() * sizeof(T), 0);
    }

    template<typename T>
    inline
    void RegisterSampleVariable(T *var, const std::string& name, SampleVariableCategory cat, size_t sz)
    {
        RegisterSampleVariable(var, name, cat, SV_BINARY, sz * sizeof(T), 0);
    }

    template<typename T, typename U>
    void RegisterSampleVariable(T& var, const std::string& name, SampleVariableCategory cat, U max)
    {
        static_assert(std::is_arithmetic<T>::value == true, "This form of RegisterSampleVariable works only on scalars.");
        static_assert(is_bool<T>::value == false, "This form of RegisterSampleVariable does not work with bool");

        T _max = max;

        SampleVariableDataType t = std::is_floating_point<T>::value ? SV_FLOAT : SV_INTEGER;

        RegisterSampleVariable(&var, name, cat, t, sizeof(T), &_max);
    }

#define RegisterSampleVariableInObject(var, cat, ...)                       \
    RegisterSampleVariable(var, GetName() + ':' + (#var + 2) , cat, ##__VA_ARGS__)

#define RegisterSampleVariableInObjectWithName(var, name, cat, ...)     \
    RegisterSampleVariable(var, GetName() + ':' + name, cat, ##__VA_ARGS__)

#define DefineSampleVariable(Type, Member) \
    Type m_ ## Member

#define DefineStateVariable(Type, Member) \
    Type m_ ## Member

#define RegisterStateVariable(LValue, Name) \
    RegisterSampleVariableInObjectWithName(LValue, Name, SVC_STATE)

#define RegisterStateArray(LValue, Size, Name) \
    RegisterSampleVariableInObjectWithName(LValue, Name, SVC_STATE, Size)

#define InitSampleVariable(Member, ...)                             \
    m_ ## Member((RegisterSampleVariableInObject(m_ ## Member, ##__VA_ARGS__), 0))

#define InitStateVariable(Member, ...)                                 \
    m_ ## Member((RegisterSampleVariableInObject(m_ ## Member, SVC_STATE), ##__VA_ARGS__))

    void ListSampleVariables(std::ostream& os, const std::string &pat = "*");
    bool ReadSampleVariables(std::ostream& os, const std::string &pat = "*"); // returns "false" if no variables match.


    // BinarySampler: used by Monitor to quickly serialize scalar
    // variables to a binary format.
    class BinarySampler
    {
        typedef std::vector<std::pair<const char*, size_t> > vars_t;

        size_t   m_datasize;
        vars_t   m_vars;

    public:
        BinarySampler(std::ostream& os, const Config& config,
                      const std::vector<std::string>& pats);

        size_t GetBufferSize() const { return m_datasize; }

        void   SampleToBuffer(char *buf) const
        {
            for (auto& i : m_vars)
                for (size_t j = 0; j < i.second; ++j)
                    *buf++ = i.first[j];
        }
    };

}

#endif
