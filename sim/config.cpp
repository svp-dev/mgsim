#include "sim/config.h"
#include "programs/mgsim.h"
#include <set>
#include <map>
#include <vector>

using namespace std;

void Config::collectPropertiesByType(Config::types_t& types,
                                     Config::typeattrs_t& typeattrs)
{
    map<Symbol, set<Symbol> > collect;

    for (auto& i : m_objects)
    {
        types[m_objects[i.first]].clear();
        typeattrs[m_objects[i.first]].clear();
    }

    for (auto& i : m_objprops)
    {
        for (auto& j : i.second)
            collect[m_objects[i.first]].insert(j.first);
    }
    for (auto& i : collect)
    {
        size_t j = 0;
        for (auto k : i.second)
        {
            types[i.first].push_back(k);
            typeattrs[i.first][k] = j++;
        }
    }
}

vector<uint32_t> Config::GetConfWords()
{
    types_t types;
    typeattrs_t typeattrs;
    collectPropertiesByType(types, typeattrs);

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

    db.push_back(types.size());

    // ENTRIES
    size_t typecnt = 0;
    for (auto& i : types)
    {
        sym_backrefs[i.first].push_back(db.size());
        db.push_back(0);
        if (!i.second.empty())
            attrtable_backrefs[i.first].push_back(db.size());
        db.push_back(0);

        type_table[i.first] = typecnt++;
    }
    db[cur_next_offset] = db.size();

    // attribute tables. For each table:
    // word 0: number of entries,
    // word 1: logical offset to object type in type table
    // then entries. Each entry is a global symbol offset (1 word)
    for (auto& i : types)
    {
        if (i.second.empty())
            continue;
        // HEADER
        db.push_back(CONF_TAG_ATTRTABLE);
        cur_next_offset = db.size(); db.push_back(0);

        // CONTENT - HEADER
        attrtable_table[i.first] = db.size();
        db.push_back(i.second.size());
        db.push_back(type_table[i.first]);

        // ENTRIES
        for (auto j : i.second)
        {
            sym_backrefs[j].push_back(db.size());
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
    for (auto& i : m_objects)
    {
        // HEADER
        db.push_back(CONF_TAG_OBJECT);
        cur_next_offset = db.size(); db.push_back(0);

        // CONTENT - HEADER
        obj_table[i.first] = db.size();
        db.push_back(type_table[i.second]);

        // ENTRIES

        // collect the attributes actually defined in the
        // order defined by the type
        auto& propdescs = typeattrs.find(i.second)->second;
        vector<EntityRef> collect;
        collect.resize(propdescs.size(), 0);

        auto op = m_objprops.find(i.first);
        if (op != m_objprops.end())
        {
            auto& props = op->second;
            for (auto& j : props)
            {
                collect[propdescs.find(j.first)->second] = j.second;
            }
        }

        // populate according to logical attribute order defined by type
        for (auto j : collect)
        {
            if (j != 0)
            {
                const Entity& e = *j;
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

    auto rawconf = getRawConfiguration();

    // HEADER
    db.push_back(CONF_TAG_RAWCONFIG);
    cur_next_offset = db.size(); db.push_back(0);

    // CONTENT - HEADER

    db.push_back(rawconf.size());

    // ENTRIES
    for (auto& i : rawconf)
    {
        Symbol key = makeSymbol(i.first);
        Symbol val = makeSymbol(i.second);
        sym_backrefs[key].push_back(db.size());
        db.push_back(0);
        sym_backrefs[val].push_back(db.size());
        db.push_back(0);
    }
    db[cur_next_offset] = db.size();

    // symbols. For each symbol:
    // word 0: size (number of characters)
    // then characters, nul-terminated

    for (auto& i : m_symbols)
    {
        // HEADER
        db.push_back(CONF_TAG_SYMBOL);
        cur_next_offset = db.size(); db.push_back(0);

        // CONTENT - HEADER
        sym_table[&i] = db.size();
        db.push_back(i.size());

        // ENTRIES - CHARACTERS

        size_t first_pos = db.size();

        // we want to enforce byte order, so we cannot use memcpy
        // because the host might be big endian.
        vector<char> raw(i.begin(), i.end());
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
    for (auto& i : attrtable_backrefs)
        for (auto j : i.second)
            db[j] = attrtable_table[i.first];
    for (auto& i : sym_backrefs)
        for (auto j : i.second)
            db[j] = sym_table[i.first];
    for (auto& i : obj_backrefs)
        for (auto j : i.second)
            db[j] = obj_table[i.first];

    return db;
}

