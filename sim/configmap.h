// -*- c++ -*-
#ifndef CONFIGMAP_H
#define CONFIGMAP_H

#include <vector>
#include <string>
#include <utility>

/// ConfigMap: map key patterns to configuration values.
//
// This is essentially a wrapper around std::vector<pair<string,
// string>>, with an additional service: a reverse-iterable adapter so
// that program code can use "for (auto& p : c.reverse())" and iterate
// backwards.
//
class ConfigMap
{
    typedef std::vector<std::pair<std::string, std::string> > map_t;
    map_t m_map;

public:
    // Default constructor: make an empty mapping.
    ConfigMap() : m_map() {}

    // Append: place a pair composed of the *lowercase* key and the
    // value at the end of the map. The key is lowercased so as to
    // canonicalize pattern matches.
    void append(const std::string& key, const std::string& value);

    // Forward iterators
    map_t::const_iterator begin() const { return m_map.begin(); }
    map_t::const_iterator end() const { return m_map.end(); }

    // Backward iteration
    struct rmap_t {
        const map_t& m_cont;
        rmap_t(const map_t& cont) : m_cont(cont) {}
        map_t::const_reverse_iterator begin() const { return m_cont.rbegin(); }
        map_t::const_reverse_iterator end() const { return m_cont.rend(); }
    };
    rmap_t reverse() const { return m_map; }
};


#endif
