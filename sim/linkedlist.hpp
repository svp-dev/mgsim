#ifndef SIM_LINKEDLIST_HPP
#define SIM_LINKEDLIST_HPP

#include "sim/linkedlist.h"

namespace Simulator
{

    template<typename T, typename L, T L::value_type::*N>
    void LinkedList<T,L,N>::Update()
    {
        // Effect the changes made in this cycle
        if (m_popped) {
            assert(!m_empty);
            if (m_head == m_tail) {
                // Popped the last one; unnotify sensitive process
                Unnotify();
                m_empty = true;
            } else {
                m_head = m_next;
            }
        }

        if (m_pushed) {
            if (m_empty) {
                // First item on the list; notify sensitive process
                m_head = m_first;
                Notify();
                m_empty = false;
            } else {
                m_table[m_tail].*N = m_first;
            }
            m_tail = m_last;
        }

        m_pushed  = false;
        m_popped  = false;
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    bool LinkedList<T,L,N>::Empty() const
    {
        return m_empty;
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    bool LinkedList<T,L,N>::Singular() const
    {
        assert(!m_empty);
        return m_head == m_tail;
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    const T& LinkedList<T,L,N>::Front() const
    {
        assert(!m_empty);
        return m_head;
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    void LinkedList<T,L,N>::Push(const T& item)
    {
        Append(item, item);
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    void LinkedList<T,L,N>::Append(const T& first, const T& last)
    {
        CheckClocks();
        assert(!m_pushed);  // We can only push once in a cycle
        MarkUpdate();
        COMMIT {
            m_first  = first;
            m_last   = last;
            m_pushed = true;
            RegisterUpdate();
        }
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    void LinkedList<T,L,N>::Pop()
    {
        CheckClocks();
        assert(!m_empty);   // We can't pop from an empty list
        assert(!m_popped);  // We can only pop once in a cycle
        MarkUpdate();
        COMMIT {
            m_popped = true;
            m_next   = m_table[m_head].*N;
            RegisterUpdate();
        }
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    LinkedList<T,L,N>::LinkedList(const std::string& name,
                                  Object& parent,
                                  Clock& clock,
                                  L& table)
        : Object(name, parent),
          Storage(name, parent, clock),
          SensitiveStorage(name, parent, clock),
          m_table(table),
          InitStateVariable(empty, true),
          InitStateVariable(popped, false),
          InitStateVariable(pushed, false),
          m_head(),
          m_tail(),
          m_next(),
          m_first(),
          m_last()
    {
        RegisterStateObject(m_head, "head");
        RegisterStateObject(m_tail, "tail");
        RegisterStateObject(m_first, "first");
        RegisterStateObject(m_last, "last");
    }


    template<typename T, typename L, T L::value_type::*N>
    inline
    bool LinkedList<T,L,N>::const_iterator::operator!=(const const_iterator& rhs) const
    {
        return !operator==(rhs);
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    bool LinkedList<T,L,N>::const_iterator::operator==(const const_iterator& rhs) const
    {
        assert(&m_table == &rhs.m_table);
        return (m_end && rhs.m_end) || (!m_end && !rhs.m_end && m_index == rhs.m_index);
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    typename LinkedList<T,L,N>::const_iterator&
    LinkedList<T,L,N>::const_iterator::operator++()
    {
        assert(!m_end);
        if (m_index == m_tail) {
            m_end = true;
        } else {
            m_index = m_table[m_index].*N;
        }
        return *this;
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    typename LinkedList<T,L,N>::const_iterator
    LinkedList<T,L,N>::const_iterator::operator++(int)
    {
        const_iterator p(*this);
        (*this)++;
        return p;
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    const T& LinkedList<T,L,N>::const_iterator::operator*() const
    {
        assert(!m_end);
        return m_index;
    }

    template<typename T, typename L, T L::value_type::*N>
    inline
    LinkedList<T,L,N>::const_iterator::const_iterator(const L& table, const T& tail, const T& index)
        : m_table(table),
          m_tail(tail),
          m_index(index),
          m_end(false)
    {}

    template<typename T, typename L, T L::value_type::*N>
    inline
    LinkedList<T,L,N>::const_iterator::const_iterator(const L& table, const T& tail)
        : m_table(table),
          m_tail(tail),
          m_index(),
          m_end(true)
    {}
}

#endif
