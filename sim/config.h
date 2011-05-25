#ifndef CONFIG_H
#define CONFIG_H

#include <algorithm>
#include <sstream>
#include <map>
#include <string>
#include <vector>
#include <ostream>
#include <cerrno>
#include <cstring>
#include <inttypes.h>
#include "kernel.h"
#include "except.h"
#include "programs/mgsim.h"

/// InputConfigRegistry: registry for configuration values provided
/// before the simulation starts.

class InputConfigRegistry
{
public:
    // a sequence of (pattern, value) pairs 
    typedef std::vector<std::pair<std::string,std::string> > ConfigMap;
    
private:
    typedef std::map<std::string,std::pair<std::string,std::string> > ConfigCache;
    
    ConfigMap         m_data;
    const ConfigMap&  m_overrides;
    ConfigCache       m_cache;

    template<typename T>
    static T convertToNumber(const std::string& name, const std::string& value)
    {
        const char *start = value.c_str();
        char *end;
        T val;

        val = strtoumax(start, &end, 0);
        if (errno == EINVAL)
        {
            val = strtoimax(start, &end, 0);
        }
        if (errno == EINVAL)
        {
            throw Simulator::exceptf<Simulator::SimulationException>("Configuration value for %s is not a number: %s", name.c_str(), start); 
        }
        if (*end != '\0')
        {
            // a prefix is specified, maybe a multiplier?
            if (0 == strcmp(end, "GiB"))
                val *= 1024ULL*1024*1024;
            else if (0 == strcmp(end, "GB"))
                val *= 1000000000ULL;
            else if (0 == strcmp(end, "MiB"))
                val *= 1024*1024;
            else if (0 == strcmp(end, "MB"))
                val *= 1000000;
            else if (0 == strcmp(end, "KiB"))
                val *= 1024;
            else if (0 == strcmp(end, "KB"))
                val *= 1000;
            else
                throw Simulator::exceptf<Simulator::SimulationException>("Configuration value for %s has an invalid suffix: %s", name.c_str(), start); 
        }
        return val;
    }

    bool lookup(const std::string& name, std::string& result, const std::string& def, bool allow_default);

    template <typename T>
    T lookupValue(const std::string& name, const T& def, bool fail_if_not_found) 
    {
        /* general version for numbers. The version for strings and bools is specialized below. */

        std::stringstream ss;
        ss << def;
        std::string val = lookupValue<std::string>(name, ss.str(), fail_if_not_found);
        
        // get the string value back
        return convertToNumber<T>(name, val);
    }

public:

    template <typename T>
    T getValueOrDefault(const std::string& name, const T& def)
    {
        return lookupValue<T>(name, def, false);
    }

    template <typename T>
    T getValueOrDefault(const Simulator::Object& obj, const std::string& name, const T& def)
    {
        return lookupValue<T>(obj.GetFQN() + ':' + name, def, false);
    }

    template <typename T>
    T getValueOrDefault(const Simulator::Object& obj, const std::string& prefix, const std::string& name, const T& def)
    {
        return lookupValue<T>(obj.GetFQN() + '.' + prefix + ':' + name, def, false);
    }

    template <typename T>
    T getValue(const std::string& name)
    {
        return lookupValue<T>(name, T(), true);
    }

    template <typename T>
    T getValue(const std::string& prefix, const std::string& name)
    {
        return lookupValue<T>(prefix + ':' + name, T(), true);
    }

    template <typename T>
    T getValue(const Simulator::Object& obj, const std::string& name)
    {
        return lookupValue<T>(obj.GetFQN() + ':' + name, T(), true);
    }

    template <typename T>
    T getValue(const Simulator::Object& obj, const std::string& prefix, const std::string& name)
    {
        return lookupValue<T>(obj.GetFQN() + '.' + prefix + ':' + name, T(), true);
    }

    std::vector<std::string> getWordList(const std::string& name); 
    std::vector<std::string> getWordList(const Simulator::Object& obj, const std::string& name);

    void dumpConfiguration(std::ostream& os, const std::string& cf) const;
    std::vector<std::pair<std::string, std::string> > getRawConfiguration() const;

    InputConfigRegistry(const std::string& filename, const ConfigMap& overrides);

};

/// specializations for the methods in InputConfigRegistry

template <>
std::string InputConfigRegistry::lookupValue<std::string>(const std::string& name, const std::string& def, bool fail_if_not_found);

template<>
double InputConfigRegistry::convertToNumber<double>(const std::string& name, const std::string& value);

template<>
float InputConfigRegistry::convertToNumber<float>(const std::string& name, const std::string& value);

template <>
bool InputConfigRegistry::convertToNumber<bool>(const std::string& name, const std::string& value);

/// ComponentModelRegistry: registry for the component structure
/// and properties after the architecture has been set up.

class ComponentModelRegistry
{

protected:
    typedef const std::string*          Symbol;
    typedef const Simulator::Object*    ObjectRef;

    struct Entity
    {
        enum { 
            VOID = CONF_ENTITY_VOID, 
            SYMBOL = CONF_ENTITY_SYMBOL,
            OBJECT = CONF_ENTITY_OBJECT,
            UINT = CONF_ENTITY_UINT, 
        } type;
        union
        {
            uint64_t     value;
            ObjectRef    object;
            Symbol       symbol;
        };

        bool operator<(const Entity& right) const;
    };

    typedef const Entity*               EntityRef;

    typedef std::pair<Symbol, EntityRef>  Property;  // function symbol -> value


    std::set<std::string>              m_symbols;   // registry for strings
    std::map<ObjectRef, Symbol>        m_objects;   // function object -> object type
    std::set<Entity>                   m_entities;  // registry for entities

    typedef std::map<ObjectRef, std::vector<Property> > objprops_t;
    objprops_t m_objprops;  // function object -> {property}*

    typedef std::map<std::pair<ObjectRef, ObjectRef>, std::vector<Property> > linkprops_t;
    linkprops_t m_linkprops; // function (object,object) -> {property}*

    void printEntity(std::ostream& os, const Entity& e) const;
    Symbol makeSymbol(const std::string& sym);
    ObjectRef refObject(const Simulator::Object& obj);
    EntityRef refEntity(const ObjectRef& obj);
    EntityRef refEntity(const Symbol& sym);
    EntityRef refEntity(const uint32_t& val);
    EntityRef refEntity(void);

    std::map<ObjectRef, Symbol> m_names;
    void renameObjects(void);

    typedef std::map<Symbol, std::vector<Symbol> > types_t;
    typedef std::map<Symbol, std::map<Symbol, size_t> > typeattrs_t;
    types_t m_types;
    typeattrs_t m_typeattrs;
    void collectPropertiesByType(void);

public:

    void registerObject(const Simulator::Object& obj, const std::string& objtype);

    void registerProperty(const Simulator::Object& obj, const std::string& name)
    {
        m_objprops[refObject(obj)].push_back(std::make_pair(makeSymbol(name), refEntity()));
    };

    template<typename T>
    void registerProperty(const Simulator::Object& obj, const std::string& name, const T& value)
    {
        m_objprops[refObject(obj)].push_back(std::make_pair(makeSymbol(name), refEntity(value)));
    };

    void registerRelation(const Simulator::Object& left, const Simulator::Object& right, const std::string& name)
    {
        m_linkprops[std::make_pair(refObject(left), refObject(right))].push_back(std::make_pair(makeSymbol(name), refEntity()));
    }

    template<typename T>
    void registerRelation(const Simulator::Object& left, const Simulator::Object& right, const std::string& name, const T& value)
    {
        m_linkprops[std::make_pair(refObject(left), refObject(right))].push_back(std::make_pair(makeSymbol(name), refEntity(value)));
    }

    template<typename T, typename U>
    void registerRelation(const T& left, const U& right, const std::string& name)
    {
        registerRelation(dynamic_cast<const Simulator::Object&>(left), dynamic_cast<const Simulator::Object&>(right), name);
    }

    template<typename T, typename U, typename V>
    void registerRelation(const T& left, const U& right, const std::string& name, const V& value)
    {
        registerRelation(dynamic_cast<const Simulator::Object&>(left), dynamic_cast<const Simulator::Object&>(right), name, value);
    }


    template<typename T, typename U>
    void registerBidiRelation(const T& left, const U& right, const std::string& name)
    {
        registerRelation(dynamic_cast<const Simulator::Object&>(left), dynamic_cast<const Simulator::Object&>(right), name);
        registerRelation(dynamic_cast<const Simulator::Object&>(right), dynamic_cast<const Simulator::Object&>(left), name);
    }

    template<typename T, typename U, typename V>
    void registerBidiRelation(const T& left, const U& right, const std::string& name, const V& value)
    {
        registerRelation(dynamic_cast<const Simulator::Object&>(left), dynamic_cast<const Simulator::Object&>(right), name, value);
        registerRelation(dynamic_cast<const Simulator::Object&>(right), dynamic_cast<const Simulator::Object&>(left), name, value);
    }


    void dumpComponentGraph(std::ostream& out, bool display_nodeprops = true, bool display_linkprops = true);
};


template<>
inline void ComponentModelRegistry::registerRelation<Simulator::Object>(const Simulator::Object& left, const Simulator::Object& right, const std::string& name, const Simulator::Object& value)
{
    m_linkprops[std::make_pair(refObject(left), refObject(right))].push_back(std::make_pair(makeSymbol(name), refEntity(refObject(value))));
}


/// Config class used by the simulation.


class Config : public InputConfigRegistry, public ComponentModelRegistry
{
public:
    Config(const std::string& filename, const ConfigMap& overrides)
        : InputConfigRegistry(filename, overrides)
    { }


    std::vector<uint32_t> GetConfWords();
};


#endif
