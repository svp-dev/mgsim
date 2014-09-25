// -*- c++ -*-
#ifndef INPUTCONFIG_H
#define INPUTCONFIG_H

#include "sim/configmap.h"
#include "sim/convertval.h"
#include "sim/except.h"
#include "sim/kernel.h"
#include <map>
#include <utility>
#include <vector>
#include <ostream>

class InputConfigRegistry
{
    // Main configuration data, typically loaded from file(s).  This
    // is matched from front to back: the first pattern that matches
    // determines the value.
    ConfigMap         m_data;

    // Overrides, typically set as command-line arguments.  This is
    // matched from back to front, prior to checking via m_data.
    ConfigMap         m_overrides;

    // Configuration cache: memoizes the values already found for
    // fully qualified names, together with the pattern that
    // matched the name (for reporting).
    typedef std::map<std::string, std::pair<std::string, std::string> > ConfigCache;
    ConfigCache              m_cache;

public:
    /// Constructor, destructor etc.
    InputConfigRegistry(const ConfigMap& data, const ConfigMap& overrides);
    virtual ~InputConfigRegistry() {}

    /// dumpConfiguration: emit the configuration data on the
    // specified stream.
    //
    // dumpConfiguration(s, cf) emits two sections: first
    // the overrides then the main configuration data.
    // The string "cf" is used to label the main configuration data.
    //
    void dumpConfiguration(std::ostream& os, const std::string& cf) const;

    /// Emit the configuration cache.
    void dumpConfigurationCache(std::ostream& os) const;

    /// getRawConfiguration: serialize the effective configuration.
    //
    // This emits a key-value vector that corresponds to the names and
    // values that were effectively retrieved so far using the lookup
    // methods, after pattern matching.
    std::vector<std::pair<std::string, std::string> > getRawConfiguration() const;

    /// GetOverrides: provide access to the overrides map.
    ConfigMap& GetOverrides() { return m_overrides; }
    const ConfigMap& GetOverrides() const { return m_overrides; }

    /// getValue: retrieve the configuration value associated to a
    // fully qualifed name.
    //
    // getValue(NAME) looks up the pattern-value database to find a
    // configuration entry whose pattern matches NAME, and returns the
    // associated value with the type specified as template paramter.
    //
    // If no pattern matches, or if the conversion to the requested
    // type fails, an exception is raised.
    //
    // If the value starts with a $ sign, it is considered as a
    // recursive lookup.
    //
    // Exists in 4 forms:
    // getValue(name)
    // getValue(prefix, name)  - same as getValue(prefix + ':' + name)
    // getValue(obj, name)     - same as getValue(obj.GetName(), name)
    // getValue(obj, prefix, name)
    //    - same as getValue(prefix, name) if obj.GetName() is empty,
    //    - otherwise getValue(obj.GetName() + '.' + prefix, name)
    //
    template <typename T>
    T getValue(const std::string& name)
    { return lookupValue<T>(name, T(), true); }

    template <typename T>
    T getValue(const std::string& prefix, const std::string& name)
    { return lookupValue<T>(prefix + ':' + name, T(), true); }

    template <typename T>
    T getValue(const Simulator::Object& obj, const std::string& name)
    { return lookupValue<T>(obj.GetName() + ':' + name, T(), true);  }

    template <typename T>
    T getValue(const Simulator::Object& obj, const std::string& prefix, const std::string& name)
    {
        auto& objname = obj.GetName();
        return lookupValue<T>((objname.empty() ? objname : objname + '.') + prefix + ':' + name, T(), true);
    }

    /// getWordList: extract a comma-separated sequence of
    // configuration strings associated with a fully qualified name.
    //
    // getWordList(NAME) looks up the pattern-value database to find a
    // configuration entry that matches NAME, and returns its value
    // considered as a comma-separated sequence of string.  Each
    // string in the vector is trimmed of white space.
    //
    // If no pattern matches, an exception is raised.
    // If a word in the value starts with a $ sign, it is
    // considered as a recursive lookup.
    //
    // Two forms are recognized:
    // getWordList(name)
    // getWordList(obj, name) - same as getWordList(obj.GetName() + ':' + name)
    //
    std::vector<std::string> getWordList(const std::string& name);

    std::vector<std::string> getWordList(const Simulator::Object& obj, const std::string& name)
    { return getWordList(obj.GetName() + ':' + name); }

    /// getValueOrDefault: retrieve the configuration value associated
    // to a fully qualifed name, or a default value.
    //
    // getValueOrDefault(NAME, DEF) looks up the pattern-value
    // database to find a configuration entry whose pattern matches
    // NAME. If a pattern matches, returns the associated value with
    // the type specified as template parameter. If no pattern
    // matches, DEF is returned.
    //
    // If the value starts with a $ sign, it is considered as a
    // recursive lookup.
    //
    // Exists in 3 forms:
    // getValueOrDefault(name, def)
    // getValueOrDefault(obj, name, def)
    //    - same as getValueOrDefault(obj.GetName() + ':' + name, def)
    // getValueOrDefault(obj, prefix, name, def)
    //    - same as getValueOrDefault(prefix, name, def) if obj.GetName() is empty,
    //    - otherwise getValueOrDEfault(obj.GetName() + '.' + prefix, name, def)
    //
    template <typename T>
    T getValueOrDefault(const std::string& name, const T& def)
    { return lookupValue<T>(name, def, false); }

    template <typename T>
    T getValueOrDefault(const Simulator::Object& obj, const std::string& name, const T& def)
    { return lookupValue<T>(obj.GetName() + ':' + name, def, false); }

    template <typename T>
    T getValueOrDefault(const Simulator::Object& obj, const std::string& prefix, const std::string& name, const T& def)
    {
        auto& objname = obj.GetName();
        return lookupValue<T>((objname.empty() ? objname : objname + '.') + prefix + ':' + name, def, false);
    }

protected:
    // lookupValue: the lookup entry point for all getValue* methods.
    //
    // lookupValue<T>(pat, def, fail_if_not_found) retrieves the value
    // associated with the pattern converted to type T.
    //
    // If fail_if_not_found is true, an exception is raised if no
    // pattern matches. Otherwise, the default value is returned.
    //
    // The generic form uses lookupValue<std::string>, specialized and
    // implemented in inputconfig.cpp. This in turn uses the lookup()
    // method.
    //
    template <typename T>
    T lookupValue(const std::string& pat, const T& def, bool fail_if_not_found)
    {
        // lookupValue<string> is specialized below.
        auto val = lookupValue<std::string>(pat, std::to_string(def), fail_if_not_found);
        T r;
        try {
            r = ConvertToNumber<T>(val);
        }
        catch (std::exception& e) {
            throw Simulator::exceptf<>("Error converting configuration value %s for %s: %s", val.c_str(), pat.c_str(), e.what());
        }
        return r;
    }

    /// lookup: the actual pattern lookup.
    //
    // lookup(name, result, def, allow_default) stores into result
    // the value associated with name.
    // Returns:
    // - true if a value was found (result = value found)
    // - true if allow_default == true and no pattern matches (result = def)
    // - false if allow_default == false and no pattern matches
    //
    bool lookup(const std::string& name, std::string& result,
                const std::string& def, bool allow_default);
};

template <>
std::string InputConfigRegistry::lookupValue<std::string>(const std::string& name, const std::string& def, bool fail_if_not_found);


#endif
