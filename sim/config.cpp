#include "config.h"
#include "except.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <fnmatch.h>
#include <cctype>

using namespace std;
using namespace Simulator;

template <>
string InputConfigRegistry::lookupValue<string>(const string& name, const string& def, bool fail_if_not_found)
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
double InputConfigRegistry::convertToNumber<double>(const string& name, const string& value)
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
float InputConfigRegistry::convertToNumber<float>(const string& name, const string& value)
{
    return convertToNumber<double>(name, value);
}

template <>
bool InputConfigRegistry::convertToNumber<bool>(const string& name, const string& value)
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



bool InputConfigRegistry::lookup(const string& name_, string& result, const string &def, bool allow_default)
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

void InputConfigRegistry::dumpConfiguration(ostream& os, const string& cf, bool raw) const
{
    if (raw)
    {
        os << "MGSimVersion=" << PACKAGE_VERSION << endl;
        for (size_t i = 0; i < m_overrides.size(); ++i)
            os << m_overrides[i].first << "=" << m_overrides[i].second << endl;
        for (ConfigCache::const_iterator p = m_cache.begin(); p != m_cache.end(); ++p)
            os << p->first << "=" << p->second.first << endl;
    }
    else
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
}

vector<string> InputConfigRegistry::getWordList(const string& name)
{
    vector<string> vals;
    istringstream stream(getValue<string>(name));
    string token = "";
    while (getline(stream, token, ','))
    {
        token.erase(remove(token.begin(), token.end(), ' '), token.end());
        vals.push_back(token);
    }
    return vals;
}

vector<string> InputConfigRegistry::getWordList(const Object& obj, const string& name)
{
    return getWordList(obj.GetFQN() + '.' + name);
}


InputConfigRegistry::InputConfigRegistry(const string& filename, const ConfigMap& overrides)
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

bool ComponentModelRegistry::Entity::operator<(const ComponentModelRegistry::Entity& right) const
{
    return (type < right.type ||
            (type == right.type && type != VOID &&
             (
                 ((type == UINT32 || type == UINT64) && value < right.value) ||
                 (type == SYMBOL && symbol < right.symbol) ||
                 (type == OBJECT && object < right.object))));
};

ComponentModelRegistry::Symbol ComponentModelRegistry::makeSymbol(const string& sym)
{
    string str(sym);
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    pair<set<string>::const_iterator, bool> i = m_symbols.insert(str);
    return &(*i.first);
}

ComponentModelRegistry::ObjectRef ComponentModelRegistry::refObject(const Simulator::Object& obj)
{
    map<ObjectRef, Symbol>::const_iterator obj_declaration = m_objects.find(&obj);
    assert(obj_declaration != m_objects.end());

    return obj_declaration->first;
}

ComponentModelRegistry::EntityRef ComponentModelRegistry::refEntity(const ObjectRef& obj)
{
    assert(m_objects.find(obj) != m_objects.end());
    Entity e;
    e.type = Entity::OBJECT;
    e.object = obj;
    pair<set<Entity>::const_iterator, bool> i = m_entities.insert(e);
    return &(*i.first);
}

ComponentModelRegistry::EntityRef ComponentModelRegistry::refEntity(const Symbol& sym)
{
    assert(m_symbols.find(*sym) != m_symbols.end());
    Entity e;
    e.type = Entity::SYMBOL;
    e.symbol = sym;
    pair<set<Entity>::const_iterator, bool> i = m_entities.insert(e);
    return &(*i.first);
}

ComponentModelRegistry::EntityRef ComponentModelRegistry::refEntity(const uint32_t& val)
{
    Entity e;
    e.type = Entity::UINT32;
    e.value = val;
    pair<set<Entity>::const_iterator, bool> i = m_entities.insert(e);
    return &(*i.first);
}

ComponentModelRegistry::EntityRef ComponentModelRegistry::refEntity(const uint64_t& val)
{
    Entity e;
    e.type = Entity::UINT64;
    e.value = val;
    pair<set<Entity>::const_iterator, bool> i = m_entities.insert(e);
    return &(*i.first);
}

ComponentModelRegistry::EntityRef ComponentModelRegistry::refEntity(void)
{
    Entity e;
    e.type = Entity::VOID;
    pair<set<Entity>::const_iterator, bool> i = m_entities.insert(e);
    return &(*i.first);
}

void ComponentModelRegistry::registerObject(const Object& obj, const string& type)
{
    assert(m_objects.find(&obj) == m_objects.end() || m_objects.find(&obj)->second == makeSymbol(type));
    m_objects[&obj] = makeSymbol(type);
}

void ComponentModelRegistry::renameObjects()
{
    map<Symbol, int> typecounters;
    m_names.clear();
    for (map<ObjectRef, Symbol>::const_iterator i = m_objects.begin(); i != m_objects.end(); ++i)
    {
        std::ostringstream ss;
        ss << *i->second;
        ss << typecounters[i->second]++;
        m_names[i->first] = makeSymbol(ss.str());
    }
}

void ComponentModelRegistry::printEntity(ostream& os, const ComponentModelRegistry::Entity& e) const
{
    switch(e.type)
    {
    case Entity::VOID: os << "(void)"; break;
    case Entity::SYMBOL: os << *e.symbol; break;
    case Entity::OBJECT: 
        assert(m_names.find(e.object) != m_names.end());
        os << *m_names.find(e.object)->second; 
        break;
    case Entity::UINT32: case Entity::UINT64: os << dec << e.value; break;
    }
}

void ComponentModelRegistry::dumpComponentGraph(ostream& os, bool display_nodeprops, bool display_linkprops) 
{
    renameObjects();

    os << "# Microgrid system topology, generated by " << PACKAGE_NAME << " " << PACKAGE_VERSION << endl
       << "digraph Microgrid {" << endl
       << "overlap=\"false\"; labeljust=\"l\"; splines=\"true\"; node [shape=box]; edge [len=2];" << endl;
    for (map<ObjectRef, Symbol>::const_iterator i = m_objects.begin(); i != m_objects.end(); ++i)
    {
        ObjectRef objref = i->first;
        os << *m_names[objref] << " [label=\"" << *m_names[objref];
        string name = objref->GetName();
        if (name != *m_names[objref])
            os << "\\nconfig:" << name;
        if (display_nodeprops && !m_objprops[objref].empty())
        {
            const vector<Property>& props = m_objprops[objref];
            for (size_t j = 0; j < props.size(); ++j)
            {
                os << "\\n" << *props[j].first;
                if (props[j].second->type != Entity::VOID)
                {
                    os << ':';
                    printEntity(os, *props[j].second);
                }
            }
        }
        os << "\"];" << endl;
    }

    for (linkprops_t::const_iterator i = m_linkprops.begin(); i != m_linkprops.end(); ++i)
    {
        const pair<ObjectRef,ObjectRef>& endpoints = i->first;
        const vector<Property>& props = i->second;

        os << *m_names[endpoints.first] << " -> " << *m_names[endpoints.second];
        if (display_linkprops)
        {
            os << " [label=\"";
            for (size_t j = 0; j < props.size(); ++j)
            {
                os << *props[j].first;
                if (props[j].second->type != Entity::VOID)
                {
                    os << ':';
                    printEntity(os, *props[j].second);
                }
                os << ' ';
            }
            os << "\"]";
        }
        os << ';'  << endl;
    }
    os << "}" << endl;
}



