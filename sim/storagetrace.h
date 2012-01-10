#ifndef STORAGETRACE_H
#define STORAGETRACE_H

#include <set>
#include <iterator>
#include <vector>
#include <iostream>

namespace Simulator
{

class Storage;

/*
 A process can write to storages in a cycle.
 To assist in deadlock analysis, we want to formalize the storages a process
 can write to. Static code analysis does not give enough information since 
 a process does not have to write to all of its storages in the same cycle.
 As such, we establish a set of possible storage traces for each process.
 
 Since a comprehensive set with all combination of valid traces is tedious
 to write, we use simple expressions between storages to aid us in creating
 this set.
 
 Possible operations:
  A ^ B     Union. Any from A or B.
  A * B     Sequence. Any from A, then any from B.
  opt(A)    Optional. Any from A or an empty trace.
*/

/// List of a storage access trace 
class StorageTrace
{
    std::vector<const Storage*> m_storages;
    
public:
    void Append(const Storage& s) {
        m_storages.push_back(&s);
    }
    
    bool operator<(const StorageTrace& rhs) const {
        return m_storages < rhs.m_storages;
    }
    
    /// Constructs an empty trace
    StorageTrace() {
    }
    
    /// Constructs a trace with one element
    StorageTrace(const Storage& a) : m_storages(1) {
        m_storages[0] = &a;
    }
    
    /// Constructs a trace from appending two traces
    StorageTrace(const StorageTrace& a, const StorageTrace& b) : m_storages(a.m_storages) {
        std::copy(b.m_storages.begin(), b.m_storages.end(), std::back_inserter(m_storages));
    }

    bool empty() const { return m_storages.empty(); }

    friend std::ostream& operator<<(std::ostream& os, const StorageTrace& st);
};

class StorageTraceSet
{
public:
    std::set<StorageTrace> m_storages;
    
    StorageTraceSet() {}
public:

    // Create cartesian product
    StorageTraceSet operator*(const StorageTraceSet& b) const
    {
        if (b.m_storages.empty())
            // Nothing to follow us? That's just us.
            return *this;
            
        if (m_storages.empty())
            // Nothing to precede b? that's just b.
            return b;
        
        StorageTraceSet res;
        for (std::set<StorageTrace>::const_iterator p =   m_storages.begin(); p !=   m_storages.end(); ++p)
        for (std::set<StorageTrace>::const_iterator q = b.m_storages.begin(); q != b.m_storages.end(); ++q)
        {
            res.m_storages.insert(StorageTrace(*p, *q));
        }
        return res;
    }
    
    StorageTraceSet& operator*=(const StorageTraceSet& a) {
        return *this = *this * a;
    }

    // Choice operator (one from N)
    StorageTraceSet& operator^=(const StorageTraceSet& a) {
        // Append all combinations in a to us
        std::copy(a.m_storages.begin(), a.m_storages.end(), std::inserter(m_storages, m_storages.end()));
        return *this;
    }
    
    bool Contains(const StorageTrace& s) const {
        return (m_storages.empty() && s.empty()) || (m_storages.find(s) != m_storages.end());
    }
    
    StorageTraceSet(const Storage& a) {
        m_storages.insert(StorageTrace(a));
    }

    StorageTraceSet(const StorageTrace& a) {
        m_storages.insert(a);
    }

    friend std::ostream& operator<<(std::ostream& os, const StorageTraceSet& st);
};

static inline StorageTraceSet operator^(const StorageTraceSet& a, const StorageTraceSet& b) {
    StorageTraceSet r(a);
    return r ^= b;
}   

static inline StorageTraceSet operator^(const Storage& a, const Storage& b) {
    return StorageTraceSet(a) ^ b;
}

static inline StorageTraceSet operator^(const Storage& a, const StorageTraceSet& sl) {
    return sl ^ a;
}

static inline StorageTraceSet operator*(const Storage& a, const Storage& b) {
    return StorageTraceSet(a) * b;
}

static inline StorageTraceSet operator*(const Storage& a, const StorageTraceSet& sl) {
    return StorageTraceSet(a) * sl;
}

static inline StorageTraceSet operator*(StorageTraceSet& sl, const Storage& a) {
    return sl * StorageTraceSet(a);
}

// Optional operator
static inline StorageTraceSet opt(const StorageTraceSet& s) {
    // Append "empty" to the combinations
    return s ^ StorageTraceSet(StorageTrace());
}

}
#endif

