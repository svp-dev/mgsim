#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <vector>
#include <utility>
#include <string>
#include <map>
#include <iostream>

#include "simtypes.h"

class SymbolTable 
{
public:
    typedef std::pair<size_t, std::string> sym_t;
    typedef std::pair<Simulator::MemAddr, sym_t> entry_t;
    
    // default constructor & copy constructor are suitable.

    void Read(std::istream& i, bool quiet = false);
    void AddSymbol(Simulator::MemAddr addr, const std::string& name, size_t sz = 0);

    void Write(std::ostream& o, const std::string& pat = "*") const;

    bool LookUp(const std::string& sym, Simulator::MemAddr &addr, bool recurse = true) const;
    
    typedef std::map<std::string, std::pair<Simulator::MemAddr, size_t> > match_t;
    match_t FindSymbols(const std::string& pat = "*") const;

    const std::string& operator[](Simulator::MemAddr addr);
    const std::string operator[](Simulator::MemAddr addr) const;
    
protected:     
    const std::string Resolve(Simulator::MemAddr addr) const;

    typedef std::vector<entry_t> table_t;
    table_t m_entries;
    typedef std::map<Simulator::MemAddr, std::string> cache_t;
    cache_t m_cache;
};


#endif
