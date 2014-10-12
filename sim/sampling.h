// -*- c++ -*-
#ifndef SAMPLING_H
#define SAMPLING_H

#include <type_traits>
#include <vector>
#include <string>
#include "sim/serialize.h"

class Config;

namespace Simulator
{
    typedef Serialization::SerializationValueType SampleVariableDataType;

    enum SampleVariableCategory {
        SVC_STATE,      // state variable
        SVC_LEVEL,      // stats: current level: cycle counter, #threads, etc
        SVC_WATERMARK,  // stats: mins and maxs, evolves monotonously
        SVC_CUMULATIVE, // stats: integral of level over time
    };


    struct Serializer
    {
        bool reading;
        union {
            std::ostream* os;
            std::istream* is;
        };

        void serialize_raw(SampleVariableDataType dt, void *d, size_t sz);

        template<typename T>
        Serializer& operator&(T& var)
        {
            Serialization::serialize_trait<T>::serialize(*this, var);
            return *this;
        }
    };

    typedef void (*serializer_func_t)(Serializer&, void*);

    /// RegisterSampleVariable: perform a registration.
    void RegisterSampleVariable(void* ptr, const std::string& name,
                                SampleVariableCategory cat,
                                SampleVariableDataType t,
                                size_t s, void* ref,
                                serializer_func_t ser);


    template<typename T>
    void RegisterSampleVariable(T& var, const std::string& name, SampleVariableCategory cat)
    {
        static_assert(std::is_arithmetic<T>::value == true
                      || std::is_enum<T>::value, "This form of RegisterSampleVariable works only on scalars, bools or enums");

        SampleVariableDataType t = \
            std::is_floating_point<T>::value ? Serialization::SV_FLOAT :
            is_bool<T>::value ? Serialization::SV_BOOL :
            Serialization::SV_INTEGER;

        RegisterSampleVariable(&var, name, cat, t, sizeof(T), 0,
                               &Serialization::serializer<Serializer, T>);
    }

    template<typename T>
    inline
    void RegisterSampleVariable(std::vector<T>& var, const std::string& name, SampleVariableCategory cat)
    {
        RegisterSampleVariable(&var[0], name, cat, Serialization::SV_BINARY, var.size() * sizeof(T), 0, 0);
    }

    template<typename T>
    inline
    void RegisterSampleVariable(T *var, const std::string& name, SampleVariableCategory cat, size_t sz)
    {
        RegisterSampleVariable(var, name, cat, Serialization::SV_BINARY, sz * sizeof(T), 0, 0);
    }

    template<typename T, typename U>
    void RegisterSampleVariable(T& var, const std::string& name, SampleVariableCategory cat, U max)
    {
        static_assert(std::is_arithmetic<T>::value == true, "This form of RegisterSampleVariable works only on scalars.");
        static_assert(is_bool<T>::value == false, "This form of RegisterSampleVariable does not work with bool");

        T _max = max;

        SampleVariableDataType t = std::is_floating_point<T>::value ? Serialization::SV_FLOAT : Serialization::SV_INTEGER;

        RegisterSampleVariable(&var, name, cat, t, sizeof(T), &_max,
                               &Serialization::serializer<Serializer, T>);
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

#define RegisterStateObject(LValue, Name) \
    RegisterSampleVariableInObjectWithName(&(LValue), Name, SVC_STATE, Serialization::SV_OTHER, 0, 0, \
                                           &Serialization::serializer<Serializer, typename std::remove_reference<decltype(LValue)>::type>)

#define RegisterStateArray(LValue, Size, Name) \
    RegisterSampleVariableInObjectWithName(LValue, Name, SVC_STATE, Size)

#define InitSampleVariable(Member, ...)                             \
    m_ ## Member((RegisterSampleVariableInObject(m_ ## Member, ##__VA_ARGS__), 0))

#define InitStateVariable(Member, ...)                                 \
    m_ ## Member((RegisterSampleVariableInObject(m_ ## Member, SVC_STATE), ##__VA_ARGS__))

    void SetSampleVariables(std::ostream& os, const std::string& pat, const std::string& val);
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
