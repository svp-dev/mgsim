// -*- c++ -*-
#ifndef SAMPLING_H
#define SAMPLING_H

#include <type_traits>
#include <vector>
#include <string>

#include <sim/serialization.h>
#include <sim/streamserializer.h>

namespace Simulator
{
    enum VariableCategory
    {
        SVC_STATE,      // state variable
        SVC_LEVEL,      // stats: current level: cycle counter, #threads, etc
        SVC_WATERMARK,  // stats: mins and maxs, evolves monotonously
        SVC_CUMULATIVE, // stats: integral of level over time
    };

    class VariableRegistry
    {
    public:
        typedef Serialization::SerializationValueType ValueType;
        typedef void (*serializer_func_t)(StreamSerializer&, void*);

    private:
        struct VarInfo
        {
            ValueType              type;
            void *                 var;
            size_t                 width;
            serializer_func_t      ser;
            VariableCategory       cat;
            std::vector<char>      max;

            VarInfo()
                : type(Serialization::SV_INTEGER), var(0), width(0), ser(0),
                  cat(SVC_LEVEL), max() {};
            VarInfo(const VarInfo&) = default;
            VarInfo& operator=(const VarInfo&) = default;
        };

        typedef std::map<std::string, VarInfo> var_registry_t;
        var_registry_t m_registry;

        const var_registry_t& GetRegistry() const { return m_registry; }

    public:
        VariableRegistry();
        VariableRegistry(const VariableRegistry&) = default;
        VariableRegistry& operator=(const VariableRegistry&) = default;

        // Register a variable.
        void RegisterVariable(void* ptr, const std::string& name,
                              VariableCategory cat,
                              ValueType t,
                              size_t s, void* ref,
                              serializer_func_t ser);
        template<typename T>
        void RegisterVariable(T& var,
                              const std::string& name,
                              VariableCategory cat);

        template<typename T, typename U>
        void RegisterVariable(T& var,
                              const std::string& name,
                              VariableCategory cat, U max);

        template<typename T>
        void RegisterVariable(std::vector<T>& var,
                              const std::string& name,
                              VariableCategory cat);
        template<typename T>
        void RegisterVariable(T *var,
                              const std::string& name,
                              VariableCategory cat, size_t sz);
        inline
        void RegisterVariable(bool *var,
                              const std::string& name,
                              VariableCategory cat, size_t sz);

        // Load a value from a string into zero or more variables.
        void SetVariables(std::ostream& os, const std::string& pat, const std::string& val) const;

        // List all variables registered whose name match the pattern.
        void ListVariables(std::ostream& os, const std::string &pat = "*") const;

        // Render all variables registered whose name match the pattern.
        // If the compact argument is true, compression is used.
        // Return false if no variable match the pattern.
        bool RenderVariables(std::ostream& os, const std::string &pat = "*",
                             bool compact = false) const;


    private:
        // Helper methods
        static
        void ListVariables_onevar(std::ostream& os,
                                  const std::string& name,
                                  const VarInfo& vinfo);
        static
        void ListVariables_header(std::ostream& os);

        friend class BinarySampler;
    };

}

#include <sim/sampling.hpp>
#include <sim/register_functions.h>

#endif
