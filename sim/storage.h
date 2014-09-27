// -*- c++ -*-
#ifndef BUFFER_H
#define BUFFER_H

#include "sim/delegate.h"
#include "sim/kernel.h"

namespace Simulator
{

    /// A storage element that needs cycle-accurate update semantics
    class Storage
        : public virtual Object
    {
        Storage*              m_next;        ///< Next pointer in the list of storages that require updates
        Clock&                m_clock;       ///< The clock that governs this storage
        bool                  m_activated;   ///< Has the storage already been activated this cycle?

    protected:

        // CheckClock: check the process using this storage runs with the
        // same clock as the storage
        void CheckClocks() const;

        // MarkUpdate: notify the current process for trace checking.
        void MarkUpdate();

        // RegisterUpdate: register this storage for an update round
        // before the next clock cycle.
        void RegisterUpdate();

    public:
        // Retrieve the next storage that requires updates.
        const Storage* GetNext() const { return m_next; }
        Storage* GetNext() { return m_next; }

        // Accessor for the clock that governs this storage.
        Clock& GetClock() const { return m_clock; }

        // Used in Kernel::UpdateStorages.
        void Deactivate() { m_activated = false; }
        virtual void Update() = 0;


        // Constructor, destructor etc.
        Storage(const std::string& name, Object& parent, Clock& clock);
        virtual ~Storage();
        Storage(const Storage&) = delete; // No copy
        Storage& operator=(const Storage&) = delete; // No assign
    };

    /// A storage element that a process is sensitive on
    class SensitiveStorage : public virtual Storage
    {
        // The process that is sensitive on this storage
        Process* m_process;

    protected:
        // Notify: Start running the process on its clock cycle.
        void Notify() { GetClock().ActivateProcess(*m_process); }

        // Unnotify: stop running the process on its clock cycle.
        void Unnotify() { m_process->Deactivate(); }

    public:
        // Attach a process to this storage
        void Sensitive(Process& process);

        // Constructor, destructor etc.
        SensitiveStorage(const std::string& name, Object& parent, Clock& clock);
        SensitiveStorage(const SensitiveStorage&) = delete; // No copy
        SensitiveStorage& operator=(const SensitiveStorage&) = delete; // No assign
    };

    std::string
    MakeStorageName(const char *prefix, const std::string& name);

#define InitStorage(Member, Clock, ...) \
    Member(MakeStorageName(decltype(Member)::NAME_PREFIX, #Member), *this, Clock, ##__VA_ARGS__)

#define MakeStorage(Type, Name, Clock, ...) \
    new Type(MakeStorageName(Type::NAME_PREFIX, Name), *this, Clock, ##__VA_ARGS__)

}

#include "sim/storage.hpp"

#endif
