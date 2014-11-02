// -*- c++ -*-
#ifndef SIM_STREAM_SERIALIZER_H
#define SIM_STREAM_SERIALIZER_H

#include <istream>
#include <ostream>
#include <sim/serialization.h>

namespace Simulator
{

    // StreamSerializer: bi-directional serialization archiver
    // using SerializationLanguage above.
    class StreamSerializer
    {
    public:
        // Indicate the direction of serialization.
        // true = from variable to stream
        // false = from stream to variable
        bool reading() const { return m_reading; }

        // Low level serializer/deserializer.
        // This is called by specializations of serialize_trait<>.
        void serialize_raw(Serialization::SerializationValueType dt,
                           void *d, size_t sz) const;

        // Low level read-only serializer
        // This is called by specializations of serialize_trait<>.
        void serialize_raw_ro(Serialization::SerializationValueType dt,
                              const void *d, size_t sz) const;

        // Serialization tagging: when reading, print the tag; when
        // writing, check the tag.  This is used as a crude, primitive
        // form of type checking when loading serialized data.
        StreamSerializer& operator&(const char* str);

        // Generalized forward to serialize_trait::serialize().
        template<typename T>
        StreamSerializer& operator&(T& var)
        {
            Serialization::serialize_trait<T>::serialize(*this, var);
            return *this;
        }

        StreamSerializer(std::ostream& os, bool compact);
        StreamSerializer(std::istream& is);

    private:
        bool m_reading;             ///< Direction of reading
        union {
            struct {
                std::ostream* m_os; ///< Stream to write to when reading
                bool m_compact;     ///< Whether to use compression
            };
            std::istream* m_is;     ///< Stream to read from when writing
        };
    };


}

#endif
