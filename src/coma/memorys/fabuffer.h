#ifndef _FABUFFER_H
#define _FABUFFER_H

#include <cstdlib>
#include <cassert>
#include <vector>
using namespace std;

// Fully Associative Matching Buffer
// data will be stored in the entry, copy will be performed
// in the current implementation, the index of a certain slot will never change
template <class Tk, class Td>
class FABuffer
{
    struct Entry
    {
        Tk                 key;
        Td                *data;
        bool               valid;
        unsigned long long used;
    };
    
    std::vector<Entry> m_entries;
    unsigned long long m_counter;

    // Try get an empty entry first, before searching the least used entry
    unsigned int GetReusableEntry()
    {
        // try get an empty entry
        for (size_t i = 0;i < m_entries.size(); ++i)
        {
            if (!m_entries[i].valid)
            {
                return i;
            }
        }
        
        // no empty slot, search for the LRU item
        unsigned long long min_value = m_entries[0].used;
        unsigned int       min_index = 0;

        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            if (m_entries[i].used < min_value)
            {
                min_value = m_entries[i].used;
                min_index = i;
            }
        }
        return min_index;
    }

public:
    // constructor
    FABuffer(unsigned int size)
        : m_entries(size), m_counter(0)
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            m_entries[i].valid = false;
            m_entries[i].data  = NULL;
        }
    }

    // destructor
    ~FABuffer()
    {
        for (unsigned int i = 0; i < m_entries.size(); ++i)
        {
            delete[] m_entries[i].data;
        }
    }

    // insert the item to the matching buffer
    // return true if inserted a new one, 
    // return false if the item already exists
    // size represent the number of elements in data
    bool InsertItem2Buffer(const Tk& key, const Td *data, size_t count)
    {
        assert(data != NULL);
        assert(count > 0);
        
        // if the entry can be found, just update the LRU
        int index;
        if (FindBufferItem(key, index) != NULL)
        {
            m_entries[index].used = m_counter++;
            return false;
        }

        index = GetReusableEntry();

        // insert here
        m_entries[index].valid = true;
        m_entries[index].key = key;
        
        // copy data
        m_entries[index].data = new Td[count];
        std::copy(data, data + count, m_entries[index].data);
        
        m_entries[index].used = m_counter++;
        return true;
    }

    Td* FindBufferItem(const Tk& key) const
    {
        int index;
        return FindBufferItem(key, index);
    }

    // find the item in the matching buffer
    Td* FindBufferItem(const Tk& key, int &index) const
    {
        for (unsigned int i = 0; i < m_entries.size(); ++i)
        {
            if (m_entries[i].valid && m_entries[i].key == key)
            {
                index = i;
                return m_entries[i].data;
            }
        }
        return NULL;
    }

    // find the item in the matching buffer
    // return -1 : failed
    // return  n : succeed, n == index
    int IsEmptySlotAvailable() const
    {
        for (unsigned int i = 0 ; i < m_entries.size(); ++i)
        {
            if (m_entries[i].valid)
            {
                return i;
            }
        }
        return -1;
    }

    // Update Buffer Item
    bool UpdateBufferItem(const Tk& key, const Td *data, size_t count)
    {
        int index;
        Td* findData;
        
        // if the entry can be found, just update the LRU
        if (FindBufferItem(key, index, findData))
        {
            m_entries[index].used = m_counter++;
            std::copy(data, data + count, m_entries[index].data);
            return true;
        }
        return false;
    }

    // remove the request in the invalidation matching buffer
    void RemoveBufferItem(const Tk& key)
    {
        int index;
        if (FindBufferItem(key, index) != NULL)
        {
            free(m_entries[index].data);
            m_entries[index].data = NULL;
            m_entries[index].valid = false;
        }
    }
};

#endif

