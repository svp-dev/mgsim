#ifndef SIM_REGISTER_HPP
#define SIM_REGISTER_HPP

#include "sim/register.h"

namespace Simulator
{

    template<typename T>
    inline
    bool Register<T>::Empty() const
    {
        return m_empty;
    }

    template<typename T>
    inline
    const T& Register<T>::Read() const
    {
        assert(!m_empty);
        return m_cur;
    }

    template<typename T>
    void Register<T>::Clear() {
        CheckClocks();
        assert(!m_cleared);     // We can only clear once in a cycle
        assert(!m_empty);       // And we should only clear a full register

        MarkUpdate();
        COMMIT {
            m_cleared = true;
            RegisterUpdate();
        }
    }

    template<typename T>
    void Register<T>::Write(const T& data)
    {
        CheckClocks();
        assert(!m_assigned);    // We can only write once in a cycle

        MarkUpdate();
        COMMIT {
            m_new      = data;
            m_assigned = true;
            RegisterUpdate();
        }
    }


    template<typename T>
    void Register<T>::Update()
    {
        // Note that if we write AND clear, we ignore the clear
        // and just do the write.
        if (m_assigned) {
            if (m_empty) {
                // Empty register became full
                Notify();
                m_empty = false;
            }
            m_cur      = m_new;
            m_assigned = false;
        } else {
            // Full register became empty
            assert(m_cleared);
            assert(!m_empty);
            Unnotify();
            m_empty = true;
        }
        m_cleared = false;
    }

    template<typename T>
    Register<T>::Register(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent),
          Storage(name, parent, clock),
          SensitiveStorage(name, parent, clock),
          m_cur(),
          m_new(),
          m_empty(true),
          m_cleared(false),
          m_assigned(false)
    {}

}

#endif
