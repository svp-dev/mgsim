#ifndef _FABUFFER_H
#define _FABUFFER_H

#include <cstdlib>
#include <cassert>
#include <vector>
using namespace std;

// Fully Associated Matching Buffer
// data will be stored in the entry, copy will be performed
// in the current implementation, the index of a certain slot will never change
template <class Tk, class Td>
class FABuffer
{
private:

    // size
    unsigned int m_nMatchingBufferSize;

    typedef struct __FABufferEntry_t{
        Tk          key;
        Td          *data;
        bool        bvalid;
        bool        lruarray[1];
    }FABufferEntry_t;

    vector<FABufferEntry_t*>    m_vecBufferData;

public:
    // constructor
    FABuffer(unsigned int size)
    {
        m_nMatchingBufferSize = size;

        for (unsigned int i=0;i<size;i++)
        {
            FABufferEntry_t* pentry = (FABufferEntry_t*)malloc(sizeof(FABufferEntry_t)+(size-1)*sizeof(bool));
            pentry->bvalid = false;
            pentry->data = NULL;
            for (unsigned int j=0;j<size;j++)
                pentry->lruarray[j] = false;

            m_vecBufferData.push_back(pentry);
        }
    }

    // destructor
    virtual ~FABuffer()
    {
        for (unsigned int i=0;i<m_vecBufferData.size();i++)
        {
            free(m_vecBufferData[i]->data);
            free(m_vecBufferData[i]);
        }
    }

protected:
    // Update buffer's LRU
    void UpdateBufferLRU(unsigned int index)
    {
        assert(index<m_nMatchingBufferSize);

        for (unsigned int i=0;i<m_nMatchingBufferSize;i++)
        {
            m_vecBufferData[index]->lruarray[i] = true;
            m_vecBufferData[i]->lruarray[index] = false;       // make sure the order
            // when i == index, then 0 (false)
        }
    }

    // get the LRU value
    virtual unsigned int GetLRUValue(unsigned int index)
    {
        assert(index<m_nMatchingBufferSize);

        // empty is always least recent used
        if (m_vecBufferData[index]->bvalid == false)
            return 0;

        unsigned int ret=0;
        // check out the sum
        for (unsigned int i=0;i<m_nMatchingBufferSize;i++)
        {
            if (m_vecBufferData[index]->lruarray[i])
                ret++;
        }

        return ret;
    }

    // get the LRU item index, try get an empty entry first, before searching the least used entry
    virtual unsigned int GetLeastRecentUsedEntryIndex()
    {
        // try get an empty entry
        for (unsigned int i=0;i<m_nMatchingBufferSize;i++)
            if (m_vecBufferData[i]->bvalid == false)
            {
                return i;
            }

        // no empty slot 
        // search for the LRU item
        unsigned int min = m_nMatchingBufferSize;
        unsigned int index=0;

        for (unsigned int i=0;i<m_nMatchingBufferSize;i++)
        {
            unsigned int lruvalue = GetLRUValue(i);
            if (lruvalue < min)
            {
                min = lruvalue;
                index = i;
            }
        }

        return index;
    }

public:
    // insert the item to the matching buffer
    // return true if inserted a new one, 
    // return false if the item already exists
    // size represent the number of elements in data
    virtual bool InsertItem2Buffer(Tk key, Td *data = NULL, unsigned int size = 0)
    {
        int index;
        Td* findData;
        // if the entry can be found, just update the LRU
        if (FindBufferItem(key, index, findData))
        {
            UpdateBufferLRU(index);
            return false;
        }

        index = GetLeastRecentUsedEntryIndex();
        if (!m_vecBufferData[index]->bvalid)
            assert(m_vecBufferData[index]->data==NULL);

        // insert here
        m_vecBufferData[index]->bvalid = true;
        m_vecBufferData[index]->key = key;
        // copy data
        if (data != NULL)
        {
            m_vecBufferData[index]->data = (Td*)malloc(size*sizeof(Td));
            assert (m_vecBufferData[index]->data != NULL);
	    memcpy(m_vecBufferData[index]->data, data, size*sizeof(Td));

        }
        UpdateBufferLRU(index);

        return true;
    }

    // find the item in the matching buffer
    virtual bool FindBufferItem(Tk key, int &index, Td *&data)
    {
        for (unsigned int i=0;i<m_nMatchingBufferSize;i++)
            if ( (m_vecBufferData[i]->bvalid == true) && (m_vecBufferData[i]->key == key) )
            {
                index = i;
                data = m_vecBufferData[i]->data;
                return true;
            }

        return false;
    }

    // find the item in the matching buffer
    // return -1 : failed
    // return  n : succeed, n == index
    virtual int IsEmptySlotAvailable()
    {
        for (unsigned int i=0;i<m_nMatchingBufferSize;i++)
        {
            if (m_vecBufferData[i]->bvalid == false)
                return i;
        }

        return -1;
    }

    // Update Buffer Item
    virtual bool UpdateBufferItem(Tk key, Td *data, unsigned int size)
    {
        int index;
        Td* findData;
        // if the entry can be found, just update the LRU
        if (FindBufferItem(key, index, findData))
        {
            UpdateBufferLRU(index);
            if (data != NULL)
            {
	      assert (m_vecBufferData[index]->data != NULL);
	      memcpy(m_vecBufferData[index]->data, data, size*sizeof(Td));

            }

            return true;
        }

        return false;
    }


    // remove the request in the invalidation matching buffer
    virtual void RemoveBufferItem(Tk key)
    {
        int index;
        Td *data;
        if (FindBufferItem(key, index, data))
        {
            free(m_vecBufferData[index]->data);
            m_vecBufferData[index]->data = NULL;
            //cout << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^" << endl;
            m_vecBufferData[index]->bvalid = false;
        }
    }
};

#endif

