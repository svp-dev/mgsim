// -*- c++ -*-
#ifndef CONFIG_H
#define CONFIG_H

#include "sim/inputconfig.h"
#include "sim/modelregistry.h"

#include <string>
#include <vector>


/// Config: access point for configuration data.
//
// This inherits InputConfigRegistry to hold a pattern-value
// repository, and ComponentModelRegistry to track high-level logical
// object-object relationships.
//
// It also holds a copy of the argv[] array for those component models
// that may need access to it (eg. ActiveROM).
//
class Config : public InputConfigRegistry, public ComponentModelRegistry
{
    std::vector<std::string> m_argv;

public:
    Config(const ConfigMap& defaults,
           const ConfigMap& overrides,
           const std::vector<std::string>& argv)
        : InputConfigRegistry(defaults, overrides),
          m_argv(argv)
    { }

    // GetArgumentVector: retrieve (a ref to) the copy of argv[].
    const std::vector<std::string>&
    GetArgumentVector() const { return m_argv; }

    // GetConfWords: generate a configuration ROM suitable for
    // introspection by the OS running on a simulated
    // architecture.
    std::vector<uint32_t> GetConfWords();

private:
    typedef std::map<Symbol, std::vector<Symbol> > types_t;
    typedef std::map<Symbol, std::map<Symbol, size_t> > typeattrs_t;
    // A helper method for GetConfWords above.
    void collectPropertiesByType(types_t&, typeattrs_t&);
};

#endif
