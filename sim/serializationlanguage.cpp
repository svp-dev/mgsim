#include <sim/except.h>
#include <sim/serializationlanguage.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cctype>

using namespace std;
namespace Simulator
{
    using namespace Serialization;
    namespace SerializationLanguage
    {
        // Forward declarations
        static
        void RenderBinaryData(ostream& os,
                              const uint8_t *buf, size_t max,
                              bool compact);
        static
        void RenderBitVector(ostream& os,
                             const bool *buf, size_t max,
                             bool compact);
        static
        void RenderBoolean(ostream& os, bool b, bool compact);
        static
        void RenderInteger(ostream& os, const void *p, size_t w,
                           bool compact);
        static
        void RenderFloat(ostream& os, const void *p, size_t w, bool compact);

        static
        void LoadBinaryData(void *buf, size_t max, istream& is);
        static
        void LoadBitVector(bool *buf, size_t max, istream& is);
        static
        void LoadBoolean(bool &var, istream& is);
        static
        void LoadInteger(void *var, size_t width, istream& is);
        static
        void LoadFloat(void *var, size_t width, istream& is);



        // RenderValue: dispatch to the appropriate render function.
        void RenderValue(ostream& os,
                         SerializationValueType t,
                         size_t w, const void *p,
                         bool compact)
        {
            os << dec;
            switch(t) {
            case SV_BINARY:
                RenderBinaryData(os, (const uint8_t*)p, w, compact);
                break;
            case SV_BITS:
                RenderBitVector(os, (const bool*)p, w, compact);
                break;
            case SV_BOOL:
                RenderBoolean(os, *(const bool*)p, compact);
                break;
            case SV_INTEGER:
                RenderInteger(os, p, w, compact);
                break;
            case SV_FLOAT:
                RenderFloat(os, p, w, compact);
                break;
            case SV_OTHER:
                throw exceptf<>("Don't know how to render");
                break;
            }
        }

        // LoadValue: dispatch to the appropriate input function
        void LoadValue(istream& is,
                       Serialization::SerializationValueType dt,
                       size_t width, void *var)
        {
            switch(dt)
            {
            case SV_BINARY:
                LoadBinaryData(var, width, is);
                break;
            case SV_BITS:
                LoadBitVector((bool*)var, width, is);
                break;
            case SV_BOOL:
                LoadBoolean(*(bool*)var, is);
                break;
            case SV_INTEGER:
                LoadInteger(var, width, is);
                break;
            case SV_FLOAT:
                LoadFloat(var, width, is);
                break;
            case SV_OTHER:
                throw exceptf<>("Don't know how to load");
            }
        }



        static
        void RenderBitVector(ostream& os,
                             const bool *buf, size_t max,
                             bool compact)
        {
            os << max << ' ';

            // Serialization language:
            // use digits '0' and '1' for false and true respectively.
            // A sequence of N>2 trues is serialized as "+N."
            // A sequence of N>2 falses is serialized as "-N."
            // An underscore is used to separate groups of 8 bits.
            const bool* s = buf;
            const bool* end = s + max;
            while (s < end)
            {
                if (!compact && s > buf && (s - buf) % 8 == 0)
                    os << '_';

                if (compact && s + 2 < end && s[1] == s[0] && s[2] == s[0])
                {
                    size_t n;
                    for (n = 2; s + n < end && s[n] == s[0]; ++n)
                        ;
                    os << (s[0] ? '+' : '-') << n << '.';
                    s += n;
                    continue;
                }

                os << (s[0] ? '1' : '0');
                ++s;
            }
        }

        static inline
        bool isokforstring(int c)
        {
            return isprint(c) && (c != '"') && !isspace(c);
        }

        static
        char uint2hex(unsigned d)
        {
            if (d < 10)
                return '0' + d;
            else
                return 'a' - 10 + d;
        }

        static
        void RenderBinaryData(ostream& os,
                              const uint8_t *buf, size_t max,
                              bool compact)
        {
            os << max << ' ';
            // Serialization language:
            // Uses two hex digits HH to represent 1 byte.
            // Uses "abc" for printable strings with less than 3
            // repeated characters in a row.
            // Uses "+Nz" for N>=2 nul bytes.
            // Uses "HH+Nr" for (N-1)>=2 repetitions of HH.
            size_t n;
            const uint8_t* s = buf;
            const uint8_t* end = s + max;
            while (s < end)
            {
                if (!compact && s > buf && (s - buf) % 8 == 0)
                    os << '_';

                if (compact && s + 2 < end
                    && isokforstring(s[0])
                    && isokforstring(s[1])
                    && isokforstring(s[2]))
                {
                    // the string ends at the first non-printable, non-dquote
                    // character, or the first character that is repeated
                    // 3 or more times
                    for (n = 0; s + n < end && isokforstring(s[n]); ++n)
                    {
                        size_t rep;
                        for (rep = 1; rep < 4 &&
                                 s + n + rep < end &&
                                 isokforstring(s[n + rep]) &&
                                 s[n + rep] == s[n]; ++rep)
                            ;
                        if (rep >= 3)
                            break;
                    }
                    if (n > 0)
                    {
                        os << '"';
                        for (size_t i = 0; i < n; ++i)
                            os << (char)s[i];
                        os << '"';
                        s += n;
                        continue;
                    }
                }
                if (compact && *s == 0 && s + 1 < end && s[1] == 0)
                {
                    for (n = 1; s + n < end && s[n] == 0; ++n)
                        ;
                    os << '+' << n << 'z';
                    s += n;
                    continue;
                }

                os << uint2hex(*s / 16) << uint2hex(*s % 16);
                if (compact)
                {
                    for (n = 1; s + n < end && s[n] == s[0]; ++n)
                        ;
                    if (n >= 2)
                    {
                        os << '+' << n-1 << 'r';
                        s += n - 1;
                    }
                }
                ++s;
            }
        }

        static
        void RenderBoolean(ostream& os, bool b, bool compact)
        {
            if (compact)
                os << (b ? '1' : '0');
            else
                os << boolalpha << b;
        }

        static
        void RenderInteger(ostream& os, const void *p, size_t w,
                           bool compact)
        {
            uint64_t v = 0;

#define CHECK_CASE(W, B) case W:                              \
            if (*(const int ## B ## _t*)p == -1) v = ~v;      \
            else v = *(const uint ## B ## _t*)p;              \
            break                                             \

            switch(w) {
                CHECK_CASE(1, 8);
                CHECK_CASE(2, 16);
                CHECK_CASE(4, 32);
                CHECK_CASE(8, 64);
            default:
                os << "xi ";
                RenderBinaryData(os, (const uint8_t*)p, w, compact);
                return;
            }
            if (compact && v < 10)
                os << v;
            else if (compact && ~v == 0)
                os << "-1";
            else
                os << "0x" << hex << v << dec;
        }

        static
        void RenderFloat(ostream& os, const void *p, size_t w, bool compact)
        {
            char buf[50];
            switch(w) {
            case sizeof(float):
                snprintf(buf, 50, "%a", (double)*(const float*)p);
                break;
            case sizeof(double):
                snprintf(buf, 50, "%a", *(const double*)p);
                break;
            case sizeof(long double):
                snprintf(buf, 50, "%La", *(const long double*)p);
                break;
            default:
                os << "xf ";
                RenderBinaryData(os, (const uint8_t*)p, w, compact);
                return;
            }
            os << buf;
        }


        static
        void LoadFloat(void *var, size_t width, istream& is)
        {
            string val;
            is >> val;
            if (val.empty())
                throw exceptf<>("Can't read float from input");
            else if (val == "xf")
                LoadBinaryData(var, width, is);
            else
                switch(width)
                {
                case sizeof(float):
                    *(float*)var = (float)stof(val, 0); break;
                case sizeof(double):
                    *(double*)var = (double)stod(val, 0); break;
                case sizeof(long double):
                    *(long double*)var = (long double)stold(val, 0); break;
                default:
                    throw exceptf<>("Don't know how to convert float of size %zu", width);
                }
        }

        static
        void LoadBoolean(bool &var, istream& is)
        {
            string s;
            is >> s;
            transform(s.begin(), s.end(), s.begin(), ::tolower);
            if (s == "yes" || s == "true" || s == "1")
                var = true;
            else if (s == "no" || s == "false" || s == "0")
                var = false;
            else
                throw exceptf<>("Invalid boolean value: %s", s.c_str());
        }

        static
        void LoadInteger(void *var, size_t width, istream& is)
        {
            string val;
            is >> val;

            if (val.empty())
                throw exceptf<>("Can't read integer from input");
            else if (val == "xi")
                LoadBinaryData(var, width, is);
            else
            {
                if (val[0] == '-')
                    switch(width)
                    {
                    case 1:
                        *(int8_t*)var = (int8_t)stoll(val, 0, 0); break;
                    case 2:
                        *(int16_t*)var = (int16_t)stoll(val, 0, 0); break;
                    case 4:
                        *(int32_t*)var = (int32_t)stoll(val, 0, 0); break;
                    case 8:
                        *(int64_t*)var = (int64_t)stoll(val, 0, 0); break;
                    default:
                        throw exceptf<>("Don't know how to convert int of size %zu", width);
                    }
                else
                    switch(width)
                    {
                    case 1:
                        *(uint8_t*)var = (uint8_t)stoull(val, 0, 0); break;
                    case 2:
                        *(uint16_t*)var = (uint16_t)stoull(val, 0, 0); break;
                    case 4:
                        *(uint32_t*)var = (uint32_t)stoull(val, 0, 0); break;
                    case 8:
                        *(uint64_t*)var = (uint64_t)stoull(val, 0, 0); break;
                    default:
                        throw exceptf<>("Don't know how to convert int of size %zu", width);
                    }
            }
        }

        static
        bool ishex(int c)
        {
            return (c >= '0' && c <= '9') ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F');
        }

        static
        unsigned hex2uint(int c)
        {
            if (c >= 'A' && c <= 'F')
                return (unsigned)(c - 'A') + 10;
            if (c >= 'a' && c <= 'f')
                return (unsigned)(c - 'a') + 10;
            return (unsigned)(c - '0');
        }

        static
        void LoadBinaryData(void *buf, size_t max, istream& is)
        {
            size_t w;
            is >> w;
            if (w != max)
                throw exceptf<>("Invalid binary blob: "
                                "expected size %zu, got %zu", max, w);

            uint8_t b = 0;
            uint8_t* start = (uint8_t*)buf;
            uint8_t* end = start + max;
            uint8_t* dst = start;
            while (dst < end)
            {
                int c = is.get();
                if (!is.good())
                    break;

                // mini-language:
                // NN = byte (hex)
                // *DDDDD. = binary value (up to 8 bits)
                // +number. = skip that number of byte in output
                // +number@ = position to that byte number in output
                // +numberz = write that number of zeros
                // +numberr = repeat the last byte that number of times
                // "..." = write the string (no escapes)
                // underscore/space: ignore
                if (c == '_' || isspace(c))
                    continue;

                else if (c == '*')
                {
                    b = 0;
                    for (c = is.get(); c == '0' || c == '1'; c = is.get())
                        b = b * 2 + (uint8_t)(c - '0');
                    if (c != '.')
                        throw exceptf<>("Invalid binary value in input");
                    *dst++ = b;
                }

                else if (c == '"')
                {
                    for (c = is.get(); c != '"' && dst < end; c = is.get())
                        *dst++ = b = (uint8_t)c;
                    if (c != '"')
                        throw exceptf<>("Invalid string in binary input");
                }

                else if (c == '+')
                {
                    size_t n = 0;
                    for (c = is.get(); c >= '0' && c <= '9'; c = is.get())
                        n = n * 10 + (size_t)(c - '0');
                    if (c == '.')
                        dst += n;
                    else if (c == '@')
                    {
                        if (n > max)
                            throw exceptf<>("Invalid offset in +@: %zu", n);
                        dst = start + n;
                    }
                    else if (c == 'z' || c == 'Z')
                        for (size_t i = 0; i < n && dst < end; ++i)
                            *dst++ = b = 0;
                    else if (c == 'r' || c == 'R')
                        for (size_t i = 0; i < n && dst < end; ++i)
                            *dst++ = b;
                    else
                        throw exceptf<>("Invalid repeat: %c", c);
                }

                else if (ishex(c))
                {
                    b = hex2uint(c);
                    if (!ishex(c = is.get()))
                        throw exceptf<>("Invalid hex digit: %c", c);
                    b = b * 16 + hex2uint(c);
                    *dst++ = b;
                }

                else
                    throw exceptf<>("Invalid character "
                                    "in binary input: %c", c);
            }
        }


        static
        void LoadBitVector(bool *buf, size_t max, istream& is)
        {
            size_t w;
            is >> w;
            if (w != max)
                throw exceptf<>("Invalid bitvec blob: "
                                "expected size %zu, got %zu", max, w);

            bool* start = buf;
            bool* end = start + max;
            bool* dst = start;
            while (dst < end)
            {
                int c = is.get();
                if (!is.good())
                    break;

                // mini-language:
                // N = bit (0/1)
                // +number. = set that number of bits to 1
                // -number. = set that number of bits to 0
                // underscore/space: ignore
                if (c == '_' || isspace(c))
                    continue;

                else if (c == '+' || c == '-')
                {
                    size_t n = 0;
                    bool bit = (c == '+') ? true : false;
                    for (c = is.get(); c >= '0' && c <= '9'; c = is.get())
                        n = n * 10 + (size_t)(c - '0');
                    if (c == '.')
                        for (size_t i = 0; i < n && dst < end; ++i)
                            *dst++ = bit;
                    else
                        throw exceptf<>("Invalid repeat: %c", c);
                }

                else if (c == '1' || c == '0')
                    *dst++ = (c == '1');

                else
                    throw exceptf<>("Invalid character "
                                    "in bitvec input: %c", c);
            }
        }



    }
}
