#include "sim/storage.h"
#include <cctype>

using namespace std;

namespace Simulator
{
    string
    MakeStorageName(const char *prefix, const std::string& name)
    {
        string r = prefix;
        size_t i = 0;
        if (name.size() > 2 && name[1] == '_')
            i = 2;

        r += (char)::tolower(name[i]);
        for (++i; i < name.size(); ++i)
        {
            if (::isupper(name[i]))
            {
                r += '_';
                r += (char)::tolower(name[i]);
            }
            else
                r += name[i];
        }
        return r;
    }

    Storage::Storage(const std::string& name, Object& parent, Clock& clock)
        : Object(name, parent),
          m_next(NULL),
          m_clock(clock),
          InitStateVariable(activated, false)
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
