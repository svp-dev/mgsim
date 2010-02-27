#include "symtable.h"
#include <algorithm>
#include <sstream>
#include <fnmatch.h>

using namespace std;
using namespace Simulator;

#define entry_addr(E) (E).first
#define entry_sz(E) (E).second.first
#define entry_sym(E) (E).second.second

const string SymbolTable::Resolve(MemAddr addr) const
{
    ostringstream o;
    // cerr << "searching for: " << hex << addr << endl;
    o << '<' << hex;

    /* if no entries in table, nothing to resolve */
    if (m_entries.empty()) 
    {
        o << "0x" << addr;
    }
    else
    {
        /* first, search */
        size_t len = m_entries.size();
        size_t cursor = 0;
        size_t half, middle;
        
        while (len > 0) 
        {
            half = len / 2;
            middle = cursor + half;
            if (entry_addr(m_entries[middle]) < addr) 
            {
                cursor = middle + 1;
                len = len - half - 1;
            }
            else
                len = half;
        }

        if (cursor >= m_entries.size()) 
        {
            // cerr << "found after last" << endl;
            // addr greater than all addresses
            // take last entry as reference
            const entry_t & last = m_entries[m_entries.size()-1];

            // is it within last entry?
            if (addr < (entry_addr(last) + entry_sz(last)))
                o << entry_sym(last) << "+0x" << entry_addr(last) + entry_sz(last) - addr;
            else
                // nothing to resolve
                o << addr;
        }
        else
        {
            // an entry was found
            const entry_t & found = m_entries[cursor];

            // do we have exact match?
            if (entry_addr(found) == addr) 
            {
                // yes, collate all equivalent symbols
                o << entry_sym(found);
                for (size_t more = cursor+1; entry_addr(m_entries[more]) == addr; ++more)
                    o << ',' << entry_sym(m_entries[more]);
            }
            else
            {
                // no exact match, will report the upper or lower
                // bound depending which is closer, with preference
                // for the lower bound

                const entry_t & above = found;

                // maybe there is no entry below, so simulate one
                entry_t below(MemAddr(0), make_pair(size_t(0), string("0")));
                if (cursor > 0)
                    below = m_entries[cursor-1];

                /*
                cerr << "bracket: " << hex << entry_addr(below) << ':' << entry_sym(below)
                     << " < " << addr << " < " << entry_addr(above) << ':' << entry_sym(above) << endl;
                */

                // are we within the lower object?
                if (addr < (entry_addr(below) + entry_sz(below))) 
                    // yes, then prefer that always
                    o << entry_sym(below) << "+0x" << (addr - entry_addr(below));
                else 
                {
                    // compute distance from both
                    size_t d_above = entry_addr(above) - addr;
                    size_t d_below = addr - (entry_addr(below) + entry_sz(below));

                    if (d_above < d_below) 
                        // using above entry
                        o << entry_sym(above) << "-0x" << d_above;
                    else
                    {
                        o << entry_sym(below);
                        if (entry_sz(below))
                            o << '+' << dec << entry_sz(below);
                        o << "+0x" << hex << d_below;
                    }
                }
            }
        }
    }   
    o << '>';
    return o.str();
}

const string SymbolTable::operator[](MemAddr addr) const
{
    /* if already in cache, get it from there */
    cache_t::const_iterator i = m_cache.find(addr);
    if (i != m_cache.end())
        return i->second;
    /* can't use cache, so simply resolve */
    return Resolve(addr);    
}

const string& SymbolTable::operator[](MemAddr addr)
{
    /* if already in cache, get it from there */
    cache_t::const_iterator i = m_cache.find(addr);
    if (i != m_cache.end())
        return i->second;
    /* cache result before return */
    return m_cache[addr] = Resolve(addr);
}

void SymbolTable::Write(std::ostream& o, const std::string& pat) const
{
    for (table_t::const_iterator i = m_entries.begin(); i != m_entries.end(); ++i) 
    {
        if (!fnmatch(pat.c_str(), entry_sym(*i).c_str(), 0)) 
        {
            o << hex << entry_addr(*i) << ' ' << entry_sym(*i);
            if (entry_sz(*i))
                o << " (" << dec << entry_sz(*i) << ')';
            o << endl;
        }
    }
}

bool SymbolTable::LookUp(const std::string& sym, MemAddr &addr, bool recurse) const
{
    for (table_t::const_iterator i = m_entries.begin(); i != m_entries.end(); ++i)
        if (entry_sym(*i) == sym)
        {
            addr = entry_addr(*i);
            return true;
        }

    for (cache_t::const_iterator i = m_cache.begin(); i != m_cache.end(); ++i)
        if (i->second == sym)
        {
            addr = i->first;
            return true;
        }

    if (recurse)
        return LookUp('<' + sym + '>', addr, false);
    return false;
}

void SymbolTable::AddSymbol(MemAddr addr, const std::string& name, size_t sz)
{
    m_entries.push_back(make_pair(addr, make_pair(sz, name)));
    sort(m_entries.begin(), m_entries.end());
    m_cache.clear();
}

void SymbolTable::Read(std::istream& i)
{
    // get entries from stream
    // assume POSIX nm format: <sym> <type> <addr> <size(opt)>

    size_t nread = 0;
    while (!i.eof()) 
    {
        MemAddr addr;
        size_t sz;
        string sym, ty;
        i.clear();
        i >> sym >> ty >> hex >> addr;
        if (!i.good()) break;
        i >> sz;
        if (!i.good()) sz = 0;

        m_entries.push_back(make_pair(addr, make_pair(sz, sym)));
        ++nread;
    }

    if (!nread)
        cerr << "#warning: no symbols read." << endl;
    else
        clog << "# " << nread << " symbols loaded." << endl;

    sort(m_entries.begin(), m_entries.end());
    m_cache.clear();
}

