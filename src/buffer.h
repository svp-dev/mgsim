#ifndef BUFFER_H
#define BUFFER_H

#include "kernel.h"
#include <deque>

namespace Simulator
{

typedef size_t  BufferSize; // Size of buffer, in elements
static const    BufferSize INFINITE = (size_t)-1;

template <typename T>
class Buffer
{
    Kernel&         m_kernel;
    size_t          m_maxSize;
    std::deque<T>   m_data;

    bool Full() const {
        return (m_maxSize != INFINITE && m_data.size() == m_maxSize);
    }
    
public:
    // We define an iterator for debugging the contents only
    typedef typename std::deque<T>::const_iterator const_iterator;
    const_iterator begin() const { return m_data.begin(); }
    const_iterator end()   const { return m_data.end(); }

    bool     Empty() const { return m_data.empty(); }
    const T& Front() const { return m_data.front(); }
          T& Front()       { return m_data.front(); }

    void Pop()
    {
        if (m_kernel.GetCyclePhase() == PHASE_COMMIT) {
            m_data.pop_front();
        }
    }
    
    bool Push(const T& item)
    {
        if (!Full())
        {
            if (m_kernel.GetCyclePhase() == PHASE_COMMIT) {
                m_data.push_back(item);
            }
            return true;
        }
        return false;
    }

    Buffer(Kernel& kernel, BufferSize maxSize) : m_kernel(kernel), m_maxSize(maxSize)
    {
    }
};

}
#endif

