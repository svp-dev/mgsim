// -*- c++ -*-
#ifndef SIM_SERIALIZATION_LANGUAGE_H
#define SIM_SERIALIZATION_LANGUAGE_H

#include <istream>
#include <ostream>
#include <cstddef>

#include <sim/serialization.h>

namespace Simulator
{
    namespace SerializationLanguage
    {
        // Render: render a simple value to an output stream.
        // Arguments:
        // - os: the stream to output the value to.
        // - type: the type of value to output (SV_...).
        // - width: for SV_FLOAT/SV_INTEGER, the width in bytes; for
        //   SV_BINARY/SV_BITS, the width in elements; unused for
        //   SV_BOOL.
        // - value: a pointer to the variable to read the value from.
        // - compact: whether to use compression on the output.
        void RenderValue(std::ostream& os,
                         Serialization::SerializationValueType type,
                         size_t width, const void *var,
                         bool compact);

        // LoadValue: read a simple value from an output stream.
        // Arguments:
        // - is: the stream to read the value from
        // - type: the type of value to input (SV_...).
        // - value: a pointer to the variable to modify.
        // - width : for SV_FLOAT/SV_INTEGER, the width in bytes; for
        //   SV_BINARY/SV_BITS, the width in elements; unused for
        //   SV_BOOL.  The width must be known in advance of calling
        //   LoadValue().
        void LoadValue(std::istream& is,
                       Serialization::SerializationValueType type,
                       size_t width, void *var);
    }
}
#endif
