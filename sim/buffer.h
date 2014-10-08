// -*- c++ -*-
#ifndef SIM_BUFFER_H
#define SIM_BUFFER_H

#include <deque>
#include "sim/storage.h"
#include "sim/sampling.h"

namespace Simulator
{
    typedef size_t  BufferSize; // Size of buffer, in elements
    static const    BufferSize INFINITE = (size_t)-1;

    /// A FIFO storage queue.
    template <typename T>
    class Buffer : public SensitiveStorage
    {
        // Maximum for m_maxPushes
        // In hardware it can be possible to support multiple pushes
        static const size_t MAX_PUSHES = 4;

        const size_t  m_maxSize;         ///< Maximum size of this buffer
        const size_t  m_maxPushes;       ///< Maximum number of pushes at a cycle
        std::deque<T> m_data;            ///< The actual buffer storage
        size_t        m_pushes;          ///< Number of items Push()'d this cycle
        T             m_new[MAX_PUSHES]; ///< The items being pushed (when m_pushes > 0)
        bool          m_popped;          ///< Has a Pop() been done?

        // Statistics
        DefineSampleVariable(uint64_t, stalls);  ///< Number of stalls so far
        DefineSampleVariable(CycleNo, lastcycle); ///< Cycle no of last event
        DefineSampleVariable(uint64_t, totalsize); ///< Cumulated current size * cycle no
        DefineSampleVariable(BufferSize, maxeffsize); ///< Maximum effective queue size reached
        DefineSampleVariable(BufferSize, cursize);  ///< Current size

    protected:
        // Update: update the buffer between cycles.
        void Update() override;

    public:
        // How many slots are there in total?
        BufferSize GetMaxSize() const;

        // Empty: returns true iff the buffer is empty.
        bool Empty() const;

        // Access the first element in the buffer.
        const T& Front() const;

        // Pop the front element from the buffer.
        void Pop();

        // Pushes the item onto the buffer. Only succeeds if at
        // least min_space space is available before the push.
        bool Push(const T& item, size_t min_space = 1);

        // Constructor
        Buffer(const std::string& name, Object& parent, Clock& clock,
               BufferSize maxSize, size_t maxPushes = 1);

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        static constexpr const char* NAME_PREFIX = "b_";

        // We define an iterator for debugging the contents only
        typedef typename std::deque<T>::const_iterator         const_iterator;
        typedef typename std::deque<T>::const_reverse_iterator const_reverse_iterator;
        const_iterator         begin()  const { return m_data.begin(); }
        const_iterator         end()    const { return m_data.end();   }
        const_reverse_iterator rbegin() const { return m_data.rbegin(); }
        const_reverse_iterator rend()   const { return m_data.rend();   }
        BufferSize size()  const { return m_data.size(); }

    };

#define InitBuffer(Member, Clock, Key, ...) \
    Member(MakeStorageName(decltype(Member)::NAME_PREFIX, #Member), *this, Clock, GetConf(Key, BufferSize), ##__VA_ARGS__)

}

#include "sim/buffer.hpp"

#endif
