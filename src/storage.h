#ifndef BUFFER_H
#define BUFFER_H

#include "delegate.h"
#include "ports.h"
#include "kernel.h"
#include "sampling.h"
#include <deque>

namespace Simulator
{

typedef size_t  BufferSize; // Size of buffer, in elements
static const    BufferSize INFINITE = (size_t)-1;

/// A storage element that needs cycle-accurate update semantics
class Storage : public virtual Object
{
    friend class Kernel;

    bool                  m_activated;   ///< Has the storage already been activated this cycle?
    Storage*              m_next;        ///< Next pointer in the list of storages that require updates

protected:
    
    // The process using this storage should run with the same clock as this storage
    void CheckClocks() {
#ifndef NDEBUG
        const Process* process = GetKernel()->GetActiveProcess();
        if (process != NULL) {
            assert(&process->GetObject()->GetClock() == &GetClock());
        }
#endif
    }
        
    void RegisterUpdate() {
        if (!m_activated) {
            m_next = GetClock().ActivateStorage(*this);
            m_activated = true;
        }
    }
    
    virtual ~Storage() {
    }
    
public:
    virtual void Update() = 0;

    Storage(const std::string& name, Object& parent, Clock& clock)
        : Object(name, clock), m_activated(false), m_next(NULL)
    {}
};

/// A storage element that a process is sensitive on
class SensitiveStorage : virtual public Storage
{
    Process* m_process; ///< The process that is sensitive on this storage object

protected:
    void Notify()
    {
        assert(m_process != NULL);
        GetClock().ActivateProcess(*m_process);
    }

    void Unnotify()
    {
        assert(m_process != NULL);
        m_process->Deactivate();
    }

public:
    void Sensitive(Process& process)
    {
        assert(m_process == NULL);
        m_process = &process;
    }

    SensitiveStorage(const std::string& name, Object& parent, Clock& clock)
      : Object(name, parent, clock), Storage(name, parent, clock), m_process(NULL)
    {
    }
};

template <
    typename          T, ///< The index type
    typename          L, ///< The lookup table type
    T L::value_type::*N  ///< The next field in the table's element type
>
class LinkedList : public SensitiveStorage
{
    L&   m_table;     ///< The table to dereference to form the linked list
    bool m_empty;     ///< Whether this list is empty
    T    m_head;      ///< First item on the list (when !m_empty)
    T    m_tail;      ///< Last item on the list (when !m_empty)
    bool m_popped;    ///< Has a Pop() been done?
    T    m_next;      ///< The next field to use (when m_popped)
    bool m_pushed;    ///< Has a Push() been done?
    T    m_first;     ///< First item of the list being pushed (when m_pushed)
    T    m_last;      ///< Last item of the list being pushed (when m_pushed)
    
    void Update()
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
    
public:
    // A forward iterator (for debugging the contents)
    struct const_iterator
    {
        const L& m_table;
        bool     m_end;
        const T& m_tail;
        T        m_index;
        
    public:
        bool operator != (const const_iterator& rhs) const { return !operator==(rhs); }
        bool operator == (const const_iterator& rhs) const {
            assert(&m_table == &rhs.m_table);
            return (m_end && rhs.m_end) || (!m_end && !rhs.m_end && m_index == rhs.m_index);
        }
        const_iterator& operator++() {
            assert(!m_end);
            if (m_index == m_tail) {
                m_end = true;
            } else {
                m_index = m_table[m_index].*N;
            }
            return *this;
        }
        const_iterator operator++(int) { const_iterator p(*this); operator++(); return p; }
        const T& operator*() const { assert(!m_end); return m_index; }
        const_iterator(const L& table, const T& tail, const T& index) : m_table(table), m_end(false), m_tail(tail), m_index(index) {}
        const_iterator(const L& table, const T& tail)                 : m_table(table), m_end(true ), m_tail(tail) {}
    };
    
    const_iterator begin() const { return const_iterator(m_table, m_tail, m_head);  }
    const_iterator end()   const { return const_iterator(m_table, m_tail); }
    
    /// Is the list empty?
    bool Empty() const
    {
        return m_empty;
    }
    
    /// Does the list contain only one item?
    bool Singular() const
    {
        assert(!m_empty);
        return m_head == m_tail;
    }
    
    /// Returns the front index on the list
    const T& Front() const {
        assert(!m_empty);
        return m_head;
    }
    
    /// Pushes the item on the back of the list
    void Push(const T& item)
    {
        Append(item, item);
    }

    /// Appends the passed list to this list
    void Append(const T& first, const T& last)
    {
        CheckClocks();        
        assert(!m_pushed);  // We can only push once in a cycle
        COMMIT {
            m_first  = first;
            m_last   = last;
            m_pushed = true;
            RegisterUpdate();
        }
    }
    
    /// Removes the front item from this list
    void Pop()
    {
        CheckClocks();
        assert(!m_empty);   // We can't pop from an empty list
        assert(!m_popped);  // We can only pop once in a cycle
        COMMIT {
            m_popped = true;
            m_next   = m_table[m_head].*N;
            RegisterUpdate();
        }
    }
    
    /// Construct an empty list with a sensitive component
    LinkedList(const std::string& name, Object& parent, Clock& clock, L& table)
        : Object(name, parent, clock),
          Storage(name, parent, clock),
          SensitiveStorage(name, parent, clock),
          m_table(table), m_empty(true), m_popped(false), m_pushed(false)
    {}
};

/// A FIFO storage queue
template <typename T>
class Buffer : public SensitiveStorage
{
    // Maximum for m_maxPushes
    // In hardware it can be possible to support multiple pushes
    static const size_t MAX_PUSHES = 4;
    
    size_t        m_maxSize;         ///< Maximum size of this buffer
    size_t        m_maxPushes;       ///< Maximum number of pushes at a cycle
    std::deque<T> m_data;            ///< The actual buffer storage
    bool          m_popped;          ///< Has a Pop() been done?
    size_t        m_pushes;          ///< Number of items Push()'d this cycle
    T             m_new[MAX_PUSHES]; ///< The items being pushed (when m_pushes > 0)

    // Statistics
    uint64_t      m_stalls;         ///< Number of stalls so far
    CycleNo       m_lastcycle;      ///< Cycle no of last event
    uint64_t      m_totalsize;      ///< Cumulated current size * cycle no
    BufferSize    m_maxsize;        ///< Maximum effective queue size reached
    BufferSize    m_cursize;        ///< Current size

    void Update()
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

        COMMIT{
            // Update statistics
            CycleNo cycle = GetKernel()->GetCycleNo();
            CycleNo elapsed = cycle - m_lastcycle;
            m_lastcycle = cycle;
            m_cursize = m_data.size();
            m_totalsize += (uint64_t)m_cursize * elapsed;
            m_maxsize = std::max(m_maxsize, m_cursize);
        }
    }
    
public:
    // We define an iterator for debugging the contents only
    typedef typename std::deque<T>::const_iterator         const_iterator;
    typedef typename std::deque<T>::const_reverse_iterator const_reverse_iterator;
    const_iterator         begin()  const { return m_data.begin(); }
    const_iterator         end()    const { return m_data.end();   }
    const_reverse_iterator rbegin() const { return m_data.rbegin(); }
    const_reverse_iterator rend()   const { return m_data.rend();   }

    bool       Empty() const { return m_data.empty(); }
    const T&   Front() const { return m_data.front(); }
    BufferSize size()  const { return m_data.size(); }

    void Pop()
    {
        CheckClocks();
        assert(!m_popped);  // We can only pop once in a cycle
        COMMIT {
            m_popped = true;
            RegisterUpdate();
        }
    }
    
    // Pushes the item onto the buffer. Only succeeds if at
    // least min_space space is available before the push.
    bool Push(const T& item, size_t min_space = 1)
    {
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

    Buffer(const std::string& name, Object& parent, Clock& clock, BufferSize maxSize, size_t maxPushes = 1)
        : Object(name, parent, clock),
          Storage(name, parent, clock),
          SensitiveStorage(name, parent, clock),
          m_maxSize(maxSize), m_maxPushes(maxPushes), m_popped(false), m_pushes(0),
          m_stalls(0), m_lastcycle(0), m_totalsize(0), m_maxsize(0)
    {
        RegisterSampleVariableInObject(m_totalsize, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_maxsize, SVC_WATERMARK, maxSize);
        RegisterSampleVariableInObject(m_cursize, SVC_LEVEL);
        RegisterSampleVariableInObject(m_stalls, SVC_CUMULATIVE);
        assert(maxPushes <= MAX_PUSHES);
    }

};

/// A full/empty single-value storage element
template <typename T>
class Register : public SensitiveStorage
{
    bool m_empty;
    T    m_cur;
    T    m_new;
    bool m_cleared;
    bool m_assigned;

protected:
    void Update() {
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
    
public:
    bool Empty() const {
        return m_empty;
    }
    
    const T& Read() const {
        assert(!m_empty);
        return m_cur;
    }
    
    void Clear() {
        CheckClocks();
        assert(!m_cleared);     // We can only clear once in a cycle
        assert(!m_empty);       // And we should only clear a full register
        COMMIT {
            m_cleared = true;
            RegisterUpdate();
        }
    }
    
    void Write(const T& data) {
        CheckClocks();
        assert(!m_assigned);    // We can only write once in a cycle
        COMMIT {
            m_new      = data;
            m_assigned = true;
            RegisterUpdate();
        }
    }
    
    Register(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent, clock),
          Storage(name, parent, clock),
          SensitiveStorage(name, parent, clock),
          m_empty(true), m_cleared(false), m_assigned(false)
    {}
};

/// A single-bit storage element
class Flag : virtual public Storage
{
protected:
    bool m_set;
    bool m_updated;
    bool m_new;

    void Update() {
        m_set     = m_new;
        m_updated = false;
    }
    
public:
    bool IsSet() const {
        return m_set;
    }

    bool Set() {
        if (!m_updated) {
            COMMIT {
                m_new     = true;
                m_updated = true;
                RegisterUpdate();
            }
            return true;
        }
        return false;
    }
    
    bool Clear() {
        if (!m_updated) {
            COMMIT {
                m_new     = false;
                m_updated = true;
                RegisterUpdate();
            }
            return true;
        }
        return false;
    }
    
    Flag(const std::string& name, Object& parent, Clock& clock, bool set)
        : Object(name, parent, clock), Storage(name, parent, clock),
          m_set(false), m_updated(false), m_new(set)
    {
        if (set) {
            RegisterUpdate();
        }
    }
};

class SingleFlag : public Flag, public SensitiveStorage
{
    void Update() {
        if (m_new && !m_set) {
            this->Notify();
        } else if (m_set && !m_new) {
            this->Unnotify();
        }
        Flag::Update();
    }
public:
    SingleFlag(const std::string& name, Object& parent, Clock& clock, bool set)
        : Object(name, parent, clock),
        Storage(name, parent, clock), 
        Flag(name, parent, clock, set), 
        SensitiveStorage(name, parent, clock)
    {}
};

}
#endif
