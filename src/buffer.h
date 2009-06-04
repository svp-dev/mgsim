#ifndef BUFFER_H
#define BUFFER_H

#include "kernel.h"
#include <queue>

namespace Simulator
{

typedef size_t  BufferSize; // Size of buffer, in elements
static const    BufferSize INFINITE = (size_t)-1;

template <typename T>
class Buffer
{
    Kernel&         m_kernel;
    size_t          m_maxSize;
    std::queue<T>   m_data;

public:
    bool       empty() const { return m_data.empty(); }
    bool       full()  const { return (m_maxSize != INFINITE && m_data.size() == m_maxSize); }
    BufferSize size()  const { return m_data.size(); }
    const T& front() const { return m_data.front(); }
          T& front()       { return m_data.front(); }

    void pop() { if (m_kernel.GetCyclePhase() == PHASE_COMMIT) { m_data.pop(); } }
    bool push(const T& item)
    {
        if (!full())
        {
            if (m_kernel.GetCyclePhase() == PHASE_COMMIT) { m_data.push(item); }
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

