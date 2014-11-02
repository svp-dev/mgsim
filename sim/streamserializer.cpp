#include <sim/streamserializer.h>
#include <sim/serializationlanguage.h>
#include <sim/except.h>
#include <cassert>

using namespace std;
namespace Simulator
{
    StreamSerializer::StreamSerializer(ostream& os, bool compact)
        : m_reading(true)
    {
        m_os = &os;
        m_compact = compact;
    }

    StreamSerializer::StreamSerializer(istream &is)
        : m_reading(false)
    {
        m_is = &is;
    }

    StreamSerializer& StreamSerializer::operator&(const char *tag)
    {
        if (m_reading)
            *m_os << ' ' << tag;
        else
        {
            string s;
            *m_is >> s;
            if (s != tag)
                throw exceptf<>("Invalid serialized data: "
                                "expected tag %s, got %s", tag, s.c_str());
        }
        return *this;
    }

    void StreamSerializer::serialize_raw(Serialization::SerializationValueType dt,
                                         void* var, size_t sz) const
    {
        if (m_reading)
        {
            *m_os << ' ';
            SerializationLanguage::RenderValue(*m_os, dt, sz, var, m_compact);
        }
        else
            SerializationLanguage::LoadValue(*m_is, dt, sz, var);
    }

    void StreamSerializer::serialize_raw_ro(Serialization::SerializationValueType dt,
                                            const void* var, size_t sz) const
    {
        assert(m_reading);
        *m_os << ' ';
        SerializationLanguage::RenderValue(*m_os, dt, sz, var, m_compact);
    }


}
