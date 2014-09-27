#include "sim/storage.h"

namespace Simulator
{
    Storage::Storage(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent),
          m_next(NULL),
          m_clock(clock),
          m_activated(false)
    {}

    Storage::~Storage()
    {}

    void
    SensitiveStorage::Sensitive(Process& process)
    {
        assert(m_process == NULL);
        m_process = &process;
    }

    SensitiveStorage::SensitiveStorage(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent),
          Storage(name, parent, clock),
          m_process(NULL)
    { }


}
