#include <sstream>
#include <algorithm>
#include <cctype>
#include <fnmatch.h>
#include "sim/inputconfig.h"

using namespace std;
using namespace Simulator;

template <>
string InputConfigRegistry::lookupValue<string>(const string& name, const string& def, bool fail_if_not_found)
{
    string val;
    bool found = lookup(name, val, def, !fail_if_not_found);
    int i = 0;
    while (found && val[0] == '$')
    {
        if (i++ > 10000)
        {
            throw exceptf<>("Runaway indirection in configuration: %s", name.c_str());
        }
        string newpat = val.substr(1);
        found = lookup(newpat, val, def, !fail_if_not_found);
    }
    if (!found)
    {
        if (fail_if_not_found)
            throw exceptf<>("No configuration key matches: %s", name.c_str());
        else
            return def;
    }
    return val;
}

bool InputConfigRegistry::lookup(const string& name_, string& result, const string &def, bool allow_default)
{
    string name(name_);
    string pat;
    transform(name.begin(), name.end(), name.begin(), ::tolower);

    auto p = m_cache.find(name);
    if (p != m_cache.end())
    {
        result = p->second.first;
        return true;
    }

    bool found = false;
    for (auto& o : m_overrides.reverse())
    {
        pat = o.first;
        if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), 0))
        {
            // Return the overriden value
            result = o.second;
            found = true;
            break;
        }
    }

    if (!found)
    {
        for (auto& c : m_data.reverse())
        {
            pat = c.first;
            if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), 0))
            {
                // Return the configuration value
                result = c.second;
                found = true;
                break;
            }
        }
    }
    if (!found && allow_default)
    {
        pat = "default";
        result = def;
        found = true;
    }
    if (found)
    {
        m_cache[name] = make_pair(result, pat);
    }
    return found;
}

vector<pair<string, string> > InputConfigRegistry::getRawConfiguration() const
{
    vector<pair<string, string> > ret;
    for (auto& c : m_cache)
        ret.push_back(make_pair(c.first, c.second.first));
    return ret;
}

void InputConfigRegistry::dumpConfiguration(ostream& os, const string& cf) const
{
    os << "### begin simulator configuration" << endl
       << "# overrides from command line:" << endl;

    for (auto& o : m_overrides)
        os << "# -o " << o.first << " = " << o.second << endl;

    os << "# configuration file: " << cf << endl;

    for (auto& c : m_data)
        os << "# -o " << c.first << " = " << c.second << endl;

    os << "### end simulator configuration" << endl;
}

void InputConfigRegistry::dumpConfigurationCache(ostream& os) const
{
    os << "### begin simulator configuration (lookup matches)" << endl;

    for (auto& c : m_cache)
        os << "# " << c.first << " = " << c.second.first << " (matches " << c.second.second << ')' << endl;

    os << "### end simulator configuration (lookup matches)" << endl;
}

vector<string> InputConfigRegistry::getWordList(const string& name)
{
    vector<string> vals;
    istringstream stream(getValue<string>(name));
    string token = "";
    while (getline(stream, token, ','))
    {
        token.erase(remove(token.begin(), token.end(), ' '), token.end());

        if (!token.empty() && token[0] == '$')
        {
            auto vn = getWordList(token.substr(1));
            vals.insert(vals.end(), vn.begin(), vn.end());
        }
        else
        {
            vals.push_back(token);
        }
    }
    return vals;
}

InputConfigRegistry::InputConfigRegistry(const ConfigMap& defaults, const ConfigMap& overrides)
    : m_data(defaults), m_overrides(overrides), m_cache()
{
}
