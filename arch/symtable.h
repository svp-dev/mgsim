#ifndef SYMTABLE_H
#define SYMTABLE_H

#include "simtypes.h"

#include <vector>
#include <utility>
#include <string>
#include <map>
#include <iostream>

namespace Simulator {

class SymbolTable
{
public:
    typedef std::pair<size_t, std::string> sym_t;
    typedef std::pair<MemAddr, sym_t> entry_t;

    SymbolTable() : m_entries(), m_cache() {}
    SymbolTable(const SymbolTable&) = default;

    void AddSymbol(MemAddr addr, const std::string& name, size_t sz = 0);

    void Write(std::ostream& o, const std::string& pat = "*") const;

    bool LookUp(const std::string& sym, MemAddr &addr, bool recurse = true) const;

    const std::string& operator[](MemAddr addr);
    const std::string operator[](MemAddr addr) const;

protected:
    const std::string Resolve(MemAddr addr) const;

    typedef std::vector<entry_t> table_t;
    table_t m_entries;
    typedef std::map<MemAddr, std::string> cache_t;
    cache_t m_cache;
};

}

#endif
