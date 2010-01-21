#include "config.h"
#include "except.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>

using namespace std;
using namespace Simulator;

bool Config::getBoolean(const string& name, const bool def) const
{
    string val = getString(name, def ? "true" : "false");
    transform(val.begin(), val.end(), val.begin(), ::toupper);

    // Check for the boolean values
    if (val == "TRUE" || val == "YES") return true;
    if (val == "FALSE" || val == "NO") return false;
    
    // Otherwise, try to interpret as an integer
    int i;
    stringstream stream(val);
    stream >> i;
    return (!stream.fail() && stream.eof()) ? i != 0 : def;
}

string Config::getString(string name, const string& def) const
{
    transform(name.begin(), name.end(), name.begin(), ::toupper);
    ConfigMap::const_iterator p = m_overrides.find(name);
    if (p != m_overrides.end())
    {
        // Return the overriden value
        return p->second;
    }
    
    p = m_data.find(name);
    if (p != m_data.end())
    {
        // Return the configuration value
        return p->second;
    }
    return def;
}

void Config::dumpConfiguration(ostream& os, const string& cf) const
{
    os << "### begin simulator configuration" << endl;
    set<string> seen;
    for (ConfigMap::const_iterator p = m_overrides.begin(); p != m_overrides.end(); ++p)
    {
        seen.insert(p->first);
        os << "# -o " << p->first << "=" << p->second << endl;
    }
    os << "# (from " << cf << ')' << endl;
    for (ConfigMap::const_iterator p = m_data.begin(); p != m_data.end(); ++p)
        if (seen.find(p->first) == seen.end())
            os << "# -o " << p->first << "=" << p->second << endl;
    os << "### end simulator configuration" << endl;
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
            else if (isalpha(c) || c == '_')
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
            if (isalnum(c) || c == '_') name += (char)c;
            else 
            {
                transform(name.begin(), name.end(), name.begin(), ::toupper);
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
                    
                    m_data.insert(make_pair(name,value));
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
        m_data.insert(make_pair(name,value));
    }
}

