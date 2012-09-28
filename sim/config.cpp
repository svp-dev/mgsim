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

void ConfigMap::insert(const string& key_, const string& val)
{
    string key(key_);
    transform(key.begin(), key.end(), key.begin(), ::tolower);
    m_map.insert(m_map.begin(), make_pair(key, val));
}

void ConfigMap::append(const string& key_, const string& val)
{
    string key(key_);
    transform(key.begin(), key.end(), key.begin(), ::tolower);
    m_map.push_back(make_pair(key, val));
}

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
    string val(value);
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
    transform(name.begin(), name.end(), name.begin(), ::tolower);

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
        if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), 0))
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
            if (FNM_NOMATCH != fnmatch(pat.c_str(), name.c_str(), 0))
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

vector<pair<string, string> > InputConfigRegistry::getRawConfiguration() const
{
    vector<pair<string, string> > ret;
    for (ConfigCache::const_iterator p = m_cache.begin(); p != m_cache.end(); ++p)
        ret.push_back(make_pair(p->first, p->second.first));
    return ret;
}

void InputConfigRegistry::dumpConfiguration(ostream& os, const string& cf) const
{
    os << "### begin simulator configuration" << endl
       << "# overrides from command line:" << endl;

    for (ConfigMap::const_iterator i = m_overrides.begin(); i != m_overrides.end(); ++i)
        os << "# -o " << i->first << " = " << i->second << endl;

    os << "# configuration file: " << cf << endl;

    for (ConfigMap::const_iterator i = m_data.begin(); i != m_data.end(); ++i)
        os << "# -o " << i->first << " = " << i->second << endl;

    os << "### end simulator configuration" << endl;
}

void InputConfigRegistry::dumpConfigurationCache(ostream& os) const
{
    os << "### begin simulator configuration (lookup matches)" << endl;

    for (ConfigCache::const_iterator p = m_cache.begin(); p != m_cache.end(); ++p)
        os << "# " << p->first << " = " << p->second.first << " (matches " << p->second.second << ')' << endl;

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
        vals.push_back(token);
    }
    return vals;
}

vector<string> InputConfigRegistry::getWordList(const Object& obj, const string& name)
{
    return getWordList(obj.GetFQN() + ':' + name);
}


InputConfigRegistry::InputConfigRegistry(const string& filename, const ConfigMap& overrides, const vector<string>& argv)
    : m_overrides(overrides), m_argv(argv)
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
            else if (isalpha(c) || c == '_' || c == '*' || c == '.' || c == ':')
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
            if (isalnum(c) || c == '_' || c == '*' || c == '.' || c == ':') name += (char)c;
            else 
            {
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
                    
                    m_data.append(name, value);
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
        m_data.append(name, value);
    }
}

bool ComponentModelRegistry::Entity::operator<(const ComponentModelRegistry::Entity& right) const
{
    return (type < right.type ||
            (type == right.type && type != VOID &&
             (
                 (type == UINT && value < right.value) ||
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
    e.type = Entity::UINT;
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
    case Entity::UINT: os << dec << e.value; break;
    }
}

void ComponentModelRegistry::dumpComponentGraph(ostream& os, bool display_nodeprops, bool display_linkprops) 
{
    renameObjects();

    os << "# Microgrid system topology, generated by " << PACKAGE_NAME << " " << PACKAGE_VERSION << endl
       << "digraph Microgrid {" << endl
       << "overlap=\"false\"; labeljust=\"l\"; concentrate=\"true\"; splines=\"true\"; node [shape=box]; edge [len=2,constraint=\"false\"];" << endl;
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
        const pair<ObjectRef,pair<ObjectRef, pair<bool,bool> > >& endpoints = i->first;
        const vector<Property>& props = i->second;

        os << *m_names[endpoints.first] << " -> " << *m_names[endpoints.second.first]
           << " [constraint=\"" << (endpoints.second.second.first ? "false" : "true") << "\"";

        if (endpoints.second.second.second)
            os << ",dir=both";

        if (display_linkprops)
        {
            os << ",label=\"";
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
            os << "\"";
        }
        os << "];"  << endl;
    }
    os << "}" << endl;
}

void ComponentModelRegistry::collectPropertiesByType() 
{
    m_types.clear();
    m_typeattrs.clear();

    map<Symbol, set<Symbol> > collect;

    for (map<ObjectRef, Symbol>::const_iterator i = m_objects.begin(); i != m_objects.end(); ++i)
    {
        m_types[m_objects[i->first]].clear();
        m_typeattrs[m_objects[i->first]].clear();
    }

    for (objprops_t::const_iterator i = m_objprops.begin(); i != m_objprops.end(); ++i)
    {
        for (size_t j = 0; j < i->second.size(); ++j)
            collect[m_objects[i->first]].insert(i->second[j].first);
    }
    for (map<Symbol, set<Symbol> >::const_iterator i = collect.begin(); i != collect.end(); ++i)
    {
        size_t j;
        set<Symbol>::const_iterator k;
        for (j = 0, k = i->second.begin(); k != i->second.end(); ++k, ++j)
        {
            m_types[i->first].push_back(*k);
            m_typeattrs[i->first][*k] = j;
        }
    }
}

vector<uint32_t> Config::GetConfWords()
{
    collectPropertiesByType();

    vector<uint32_t> db;

    map<Symbol, vector<size_t> > attrtable_backrefs;
    map<Symbol, vector<size_t> > sym_backrefs;
    map<ObjectRef, vector<size_t> > obj_backrefs;

    map<Symbol, size_t> type_table;
    map<Symbol, size_t> attrtable_table;
    map<ObjectRef, size_t> obj_table;
    map<Symbol, size_t> sym_table;

    size_t cur_next_offset;

    db.push_back(CONF_MAGIC);

    // types: number of types, then type entries
    // format for each type entry:
    // word 0: global offset to symbol
    // word 1: global offset to attribute table

    // HEADER
    db.push_back(CONF_TAG_TYPETABLE); 
    cur_next_offset = db.size(); db.push_back(0);

    // CONTENT - HEADER

    db.push_back(m_types.size());

    // ENTRIES
    size_t typecnt = 0;
    for (types_t::const_iterator i = m_types.begin(); i != m_types.end(); ++i, ++typecnt)
    {
        sym_backrefs[i->first].push_back(db.size());
        db.push_back(0);
        if (!i->second.empty())
            attrtable_backrefs[i->first].push_back(db.size());
        db.push_back(0);
        
        type_table[i->first] = typecnt;
    }
    db[cur_next_offset] = db.size();

    // attribute tables. For each table: 
    // word 0: number of entries, 
    // word 1: logical offset to object type in type table
    // then entries. Each entry is a global symbol offset (1 word)
    for (types_t::const_iterator i = m_types.begin(); i != m_types.end(); ++i)
    {
        if (i->second.empty())
            continue;
        // HEADER
        db.push_back(CONF_TAG_ATTRTABLE);
        cur_next_offset = db.size(); db.push_back(0);

        // CONTENT - HEADER
        attrtable_table[i->first] = db.size();
        db.push_back(i->second.size());
        db.push_back(type_table[i->first]);

        // ENTRIES
        for (size_t j = 0; j < i->second.size(); ++j)
        {
            sym_backrefs[i->second[j]].push_back(db.size());
            db.push_back(0);
        }

        db[cur_next_offset] = db.size();
    }    

    // objects. For each object:
    // word 0: logical offset to object type in type table
    // then properties. Each property is a pair (entity type, global offset/value)
    // each property's position matches the attribute name offsets
    // in the type-attribute table.
    // 
    for (map<ObjectRef, Symbol>::const_iterator i = m_objects.begin(); i != m_objects.end(); ++i)
    {
        // HEADER
        db.push_back(CONF_TAG_OBJECT);
        cur_next_offset = db.size(); db.push_back(0);
        
        // CONTENT - HEADER
        obj_table[i->first] = db.size();
        db.push_back(type_table[i->second]);

        // ENTRIES

        // collect the attributes actually defined in the
        // order defined by the type
        const map<Symbol, size_t>& propdescs = m_typeattrs.find(i->second)->second;
        vector<EntityRef> collect;
        collect.resize(propdescs.size(), 0);
        
        objprops_t::const_iterator op = m_objprops.find(i->first);
        if (op != m_objprops.end())
        {
            const vector<Property>& props = op->second;
            for (size_t j = 0; j < props.size(); ++j)
            {
                collect[propdescs.find(props[j].first)->second] = props[j].second;
            }
        }

        // populate according to logical attribute order defined by type
        for (size_t j = 0; j < collect.size(); ++j)
        {
            if (collect[j] != 0)
            {
                const Entity& e = *collect[j];
                db.push_back(e.type);
                size_t cur_offset = db.size();
                switch(e.type)
                {
                case Entity::VOID: db.push_back(0); break;
                case Entity::SYMBOL: sym_backrefs[e.symbol].push_back(cur_offset); db.push_back(0); break;
                case Entity::OBJECT: obj_backrefs[e.object].push_back(cur_offset); db.push_back(0); break;
                case Entity::UINT: db.push_back(e.value); break;
                }
            }
            else
            {
                db.push_back(0);
                db.push_back(0);
            }
        }

        db[cur_next_offset] = db.size();
    }

    // raw configuration table.
    // word 0: number of entries.
    // then entries. Each entry is a pair (symbol, symbol)

    vector<pair<string, string> > rawconf = getRawConfiguration();

    // HEADER
    db.push_back(CONF_TAG_RAWCONFIG); 
    cur_next_offset = db.size(); db.push_back(0);

    // CONTENT - HEADER

    db.push_back(rawconf.size());

    // ENTRIES
    for (size_t i = 0; i < rawconf.size(); ++i)
    {
        Symbol key = makeSymbol(rawconf[i].first);
        Symbol val = makeSymbol(rawconf[i].second);
        sym_backrefs[key].push_back(db.size());
        db.push_back(0);
        sym_backrefs[val].push_back(db.size());
        db.push_back(0);
    }
    db[cur_next_offset] = db.size();
    
    // symbols. For each symbol:
    // word 0: size (number of characters)
    // then characters, nul-terminated

    for (set<string>::const_iterator i = m_symbols.begin(); i != m_symbols.end(); ++i)
    {
        // HEADER
        db.push_back(CONF_TAG_SYMBOL);
        cur_next_offset = db.size(); db.push_back(0);
        
        // CONTENT - HEADER
        sym_table[&(*i)] = db.size();
        db.push_back(i->size());

        // ENTRIES - CHARACTERS

        size_t first_pos = db.size();

        // we want to enforce byte order, so we cannot use memcpy
        // because the host might be big endian.
        vector<char> raw(i->begin(), i->end());
        raw.resize(((raw.size() / sizeof(uint32_t)) + 1) * sizeof(uint32_t), 0);       

        db.resize(db.size() + raw.size() / sizeof(uint32_t));
        for (size_t j = first_pos, k = 0; j < db.size(); ++j, k += 4)
        {
            uint32_t val = (uint32_t)raw[k] 
                | ((uint32_t)raw[k+1] << 8)
                | ((uint32_t)raw[k+2] << 16)
                | ((uint32_t)raw[k+3] << 24);
            db[j] = val;
        }
        
        db[cur_next_offset] = db.size();
    }

    // terminate the chain with a nul tag
    db.push_back(0);

    // now resolve all back references
    for (map<Symbol, vector<size_t> >::const_iterator i = attrtable_backrefs.begin(); i != attrtable_backrefs.end(); ++i)
        for (size_t j = 0; j < i->second.size(); ++j)
            db[i->second[j]] = attrtable_table[i->first];
    for (map<Symbol, vector<size_t> >::const_iterator i = sym_backrefs.begin(); i != sym_backrefs.end(); ++i)
        for (size_t j = 0; j < i->second.size(); ++j)
            db[i->second[j]] = sym_table[i->first];
    for (map<ObjectRef, vector<size_t> >::const_iterator i = obj_backrefs.begin(); i != obj_backrefs.end(); ++i)
        for (size_t j = 0; j < i->second.size(); ++j)
            db[i->second[j]] = obj_table[i->first];

    return db;
}

