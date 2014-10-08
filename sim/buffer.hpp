#ifndef SIM_BUFFER_HPP
#define SIM_BUFFER_HPP

#include "sim/buffer.h"
#include "sim/sampling.h"

namespace Simulator
{
    template<typename T>
    void Buffer<T>::Update()
    {
        // Effect the changes made in this cycle
        if (m_pushes > 0) {
            if (m_data.empty()) {
                // The buffer became non-empty; notify sensitive process
                Notify();
            }
            for (size_t i = 0; i < m_pushes; ++i) {
                m_data.push_back(m_new[i]);
            }
        }

        if (m_popped) {
            m_data.pop_front();
            if (m_data.empty()) {
                // The buffer became empty; unnotify sensitive process
                Unnotify();

            }
        }
        m_pushes = 0;
        m_popped = false;

        // Update statistics
        CycleNo cycle = GetKernel()->GetCycleNo();
        CycleNo elapsed = cycle - m_lastcycle;
        m_lastcycle = cycle;
        m_cursize = m_data.size();
        m_totalsize += (uint64_t)m_cursize * elapsed;
        m_maxeffsize = std::max(m_maxeffsize, m_cursize);
    }

    template<typename T>
    void Buffer<T>::Pop()
    {
        CheckClocks();
        assert(!m_popped);  // We can only pop once in a cycle
        COMMIT {
            m_popped = true;
            RegisterUpdate();
        }
    }

    template<typename T>
    inline
    BufferSize Buffer<T>::GetMaxSize() const
    {
        return m_maxSize;
    }

    template<typename T>
    inline
    bool Buffer<T>::Empty() const
    {
        return m_data.empty();
    }

    template<typename T>
    inline
    const T& Buffer<T>::Front() const
    {
        assert(!m_data.empty());
        return m_data.front();
    }

    template<typename T>
    bool Buffer<T>::Push(const T& item, size_t min_space)
    {
        MarkUpdate();

        assert(min_space >= 1);
        if (m_maxPushes != 1)
        {
            // We only support buffers with multiple pushes if the
            // buffer and the pushers are in the same clock domain.
            CheckClocks();
            assert(m_pushes < m_maxPushes);
        }
        else if (m_pushes == 1)
        {
            // We've already pushed.
            // This *COULD* be a bug, or a process trying to push to a buffer
            // that is in a different clock domain and hasn't been updated yet.
            return false;
        }

        if (m_maxSize == INFINITE || m_data.size() + m_pushes + min_space <= m_maxSize)
        {
            COMMIT {
                m_new[m_pushes] = item;
                if (m_pushes == 0) {
                    RegisterUpdate();
                }
                m_pushes++;
            }
            return true;
        }

        // Accumulate for statistics. We don't want
        // to register multiple stalls so only test during acquire.
        if (IsAcquiring())
        {
            ++m_stalls;
        }

        return false;
    }


    template<typename T>
    Buffer<T>::Buffer(const std::string& name, Object& parent, Clock& clock,
                      BufferSize maxSize, size_t maxPushes)
        : Object(name, parent),
          Storage(name, parent, clock),
          SensitiveStorage(name, parent, clock),
          m_maxSize(maxSize),
          m_maxPushes(maxPushes),
          m_data(),
          m_pushes(0),
          m_popped(false),
          InitSampleVariable(stalls, SVC_CUMULATIVE),
          InitSampleVariable(lastcycle, SVC_CUMULATIVE),
          InitSampleVariable(totalsize, SVC_CUMULATIVE),
          InitSampleVariable(maxeffsize, SVC_WATERMARK, maxSize),
          InitSampleVariable(cursize, SVC_LEVEL)
    {
        assert(maxPushes <= MAX_PUSHES);
    }

}

#endif
