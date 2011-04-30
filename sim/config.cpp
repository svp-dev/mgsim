#include "config.h"
#include "except.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <fnmatch.h>

using namespace std;
using namespace Simulator;

template <>
string Config::lookupValue<string>(const string& name, const string& def, bool fail_if_not_found)
{
    string val;
    bool found = lookup(name, val, def, !fail_if_not_found);
    if (!found)
    {
        if (fail_if_not_found)
            throw exceptf<SimulationException>("No configuration key matches: %s", name.c_str());
        else
            return def;
    }
    return val;
}

template<>
double Config::convertToNumber<double>(const string& name, const string& value)
{
    const char *start = value.c_str();
    char *end;

    double val = strtod(start, &end);
    if (*end != '\0')
    {
        throw exceptf<SimulationException>("Configuration value for %s is not a floating-point number: %s", name.c_str(), start); 
    }
    return val;
}

template<>
float Config::convertToNumber<float>(const string& name, const string& value)
{
    return convertToNumber<double>(name, value);
}

template <>
bool Config::convertToNumber<bool>(const string& name, const string& value)
{
    string val = value;
    transform(val.begin(), val.end(), val.begin(), ::toupper);

    // Check for the boolean values
    if (val == "TRUE" || val == "YES") return true;
    if (val == "FALSE" || val == "NO") return false;
    
    // Otherwise, try to interpret as an integer
    int i = convertToNumber<int>(name, val);
    return i != 0;
}



bool Config::lookup(const string& name_, string& result, const string &def, bool allow_default)
{
    string name(name_);

    string pat;
    // transform(name.begin(), name.end(), name.begin(), ::toupper);

    ConfigCache::const_iterator p = m_cache.find(name);
    if (p != m_cache.end())
    {
        result = p->second.first;
        return true;
    }

    bool found = false;
    for (ConfigMap::const_iterator p = m_overrides.begin(); p != m_overrides.end(); ++p)
    {
        pat = p->first;
        if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), FNM_CASEFOLD))
        {
            // Return the overriden value
            result = p->second;
            found = true;
            break;
        }
    }
    
    if (!found)
    {
        for (ConfigMap::const_iterator p = m_data.begin(); p != m_data.end(); ++p)
        {
            pat = p->first;
            if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), FNM_CASEFOLD))
            {
                // Return the configuration value
                result = p->second;
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

void Config::dumpConfiguration(ostream& os, const string& cf) const
{
    os << "### begin simulator configuration" << endl
       << "# overrides from command line:" << endl;
    for (size_t i = 0; i < m_overrides.size(); ++i)
        os << "# -o " << m_overrides[i].first << " = " << m_overrides[i].second << endl;
    os << "# configuration file: " << cf << endl;
    for (size_t i = 0; i < m_data.size(); ++i)
        os << "# -o " << m_data[i].first << " = " << m_data[i].second << endl;
    os << "# lookup matches:" << endl;
    for (ConfigCache::const_iterator p = m_cache.begin(); p != m_cache.end(); ++p)
        os << "# " << p->first << " = " << p->second.first << " (matches " << p->second.second << ')' << endl;
    os << "### end simulator configuration" << endl;
}

vector<string> Config::getWordList(const string& name)
{
    vector<string> vals;
    istringstream stream(getValue<string>(name));
    string token = "";
    while (getline(stream, token, ','))
    {
        vals.push_back(token);
    }
    return vals;
}

vector<string> Config::getWordList(const Object& obj, const string& name)
{
    return getWordList(obj.GetFQN() + '.' + name);
}


Config::Config(const string& filename, const ConfigMap& overrides)
    : m_overrides(overrides)
{
    enum State
    {
        STATE_BEGIN,
        STATE_COMMENT,
        STATE_NAME,
        STATE_EQUALS,
        STATE_VALUE,
    };

    State state = STATE_BEGIN;
    string name;
    string value;

    ifstream input(filename.c_str());
    if (!input.is_open())
    {
        throw FileNotFoundException(filename);
    }

    while (!input.eof())
    {
        int c = input.get();
        if (input.fail())
        {
            break;
        }

        if (state == STATE_BEGIN && !isspace(c))
        {
            if (c == '#' || c == ';')
            {
                state = STATE_COMMENT;
            }
            else if (isalpha(c) || c == '_' || c == '*' || c == '.')
            {
                state = STATE_NAME;
                name = (char)c;
            }
        }
        else if (state == STATE_COMMENT)
        {
            if (c == '\n')
            {
                state = STATE_BEGIN;
            }
        }
        else if (state == STATE_NAME)
        {
            if (isalnum(c) || c == '_' || c == '*' || c == '.') name += (char)c;
            else 
            {
                // transform(name.begin(), name.end(), name.begin(), ::toupper);
                state = STATE_EQUALS;
            }
        }
        
        if (state == STATE_EQUALS && !isspace(c))
        {
            if (c == '=') state = STATE_VALUE;
        }
        else if (state == STATE_VALUE)
        {
            if (isspace(c) && value.empty())
            {
            }
            else if (c == '\r' || c == '\n' || c == '#')
            {
                if (!value.empty())
                {
                    // Strip off all the spaces from the end
                    string::size_type pos = value.find_last_not_of("\r\n\t\v\f ");
                    if (pos != string::npos) {
                        value.erase(pos + 1);
                    }
                    
                    m_data.push_back(make_pair(name,value));
                    name.clear();
                    value.clear();
                }
                state = (c == '#') ? STATE_COMMENT : STATE_BEGIN;
            }
            else 
            {
                value = value + (char)c;
            }
        }
    }
    
    if (value != "")
    {
        m_data.push_back(make_pair(name,value));
    }
}

