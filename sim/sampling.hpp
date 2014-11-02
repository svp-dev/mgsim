#ifndef SIM_SAMPLING_HPP
#define SIM_SAMPLING_HPP

#include <sim/sampling.h>
#include <type_traits>

namespace Simulator
{

    template<typename T>
    inline
    void VariableRegistry::RegisterVariable(T& var,
                                            const std::string& name,
                                            VariableCategory cat)
    {
        static_assert(std::is_arithmetic<T>::value == true
                      || std::is_enum<T>::value,
                      "This form of RegisterVariable works only "
                      "on scalars, bools or enums");

        ValueType t =
            std::is_floating_point<T>::value ? Serialization::SV_FLOAT :
            is_bool<T>::value ? Serialization::SV_BOOL :
            Serialization::SV_INTEGER;

        RegisterVariable(&var, name, cat, t, sizeof(T), 0,
                         &Serialization::serializer<StreamSerializer, T>);
    }

    template<typename T, typename U>
    void VariableRegistry::RegisterVariable(T& var,
                                            const std::string& name,
                                            VariableCategory cat, U max)
    {
        static_assert(std::is_arithmetic<T>::value == true,
                      "This form of RegisterSampleVariable works only "
                      "on scalars.");
        static_assert(is_bool<T>::value == false,
                      "This form of RegisterSampleVariable does not work "
                      "with bool");

        T _max = max;

        ValueType t =
            std::is_floating_point<T>::value ?
            Serialization::SV_FLOAT :
            Serialization::SV_INTEGER;

        RegisterVariable(&var, name, cat, t, sizeof(T), &_max,
                         &Serialization::serializer<StreamSerializer, T>);
    }


    template<typename T>
    inline
    void VariableRegistry::RegisterVariable(std::vector<T>& var,
                                            const std::string& name,
                                            VariableCategory cat)
    {
        RegisterVariable(&var[0], name, cat,
                         Serialization::SV_BINARY,
                         var.size() * sizeof(T), 0, 0);
    }

    template<typename T>
    inline
    void VariableRegistry::RegisterVariable(T *var,
                                            const std::string& name,
                                            VariableCategory cat, size_t sz)
    {
        RegisterVariable(var, name, cat,
                         Serialization::SV_BINARY,
                         sz * sizeof(T), 0, 0);
    }

    inline
    void VariableRegistry::RegisterVariable(bool *var,
                                            const std::string& name,
                                            VariableCategory cat, size_t sz)
    {
        RegisterVariable(var, name, cat,
                         Serialization::SV_BITS, sz, 0, 0);
    }

}

#endif
