#include "sim/sampling.h"
#include "sim/config.h"
#include "sim/except.h"
#include <sys_config.h>
#include <arch/MGSystem.h>

#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <vector>
#include <ctime>
#include <fnmatch.h>
#include <unistd.h>

using namespace std;

namespace Simulator
{
    using namespace Serialization;

    struct VarInfo
    {
        SampleVariableDataType type;
        SampleVariableCategory cat;
        void *                 var;
        size_t                 width;
        vector<char>           max;
        serializer_func_t      ser;

        VarInfo() : type(SV_INTEGER), cat(SVC_LEVEL), var(0), width(0), max(), ser(0) {};
        VarInfo(const VarInfo&) = default;
        VarInfo& operator=(const VarInfo&) = default;
    };

    typedef map<string, VarInfo> var_registry_t;

    static var_registry_t& GetRegistry()
    {
        static var_registry_t registry;
        return registry;
    }

    void RegisterSampleVariable(void *var, const string& name,
                                SampleVariableCategory cat,
                                SampleVariableDataType type,
                                size_t width, void *maxval,
                                serializer_func_t ser)
    {
        auto &registry = GetRegistry();
        if (registry.find(name) != registry.end())
            throw Simulator::exceptf<>("Duplicate variable registration: %s", name.c_str());

        VarInfo vinfo;

        vinfo.var = var;
        vinfo.width = width;
        vinfo.type = type;
        vinfo.cat = cat;
        vinfo.ser = ser;

        const char *maxdata = (const char*)maxval;
        if (maxdata)
            for (size_t i = 0; i < width; ++i)
                vinfo.max.push_back(maxdata[i]);

        registry[name] = vinfo;
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
    char uint2hex(unsigned d)
    {
        if (d < 10)
            return '0' + d;
        else
            return 'a' - 10 + d;
    }

    static
    void DumpBinaryData(ostream& os, const void *buf, size_t max)
    {
        // preference:
        // - "abc" for printable strings with less than 3
        // repeated characters in a row
        // - +NNz for 2 or more nul bytes
        // - BB+NNr for 3 or more repetitions of BB
        size_t n;
        const uint8_t* s = (const uint8_t*)buf;
        const uint8_t* end = s + max;
        while (s < end)
        {
            if (s + 2 < end && isprint(s[0]) && isprint(s[1]) && isprint(s[2]))
            {
                // the string ends at the first non-printable, non-dquote
                // character, or the first character that is repeated
                // 3 or more times
                for (n = 0; s + n < end && isprint(s[n]) && s[n] != '"'; ++n)
                    {
                        size_t rep;
                        for (rep = 1; rep < 4 &&
                                 s + n + rep < end &&
                                 isprint(s[n + rep]) &&
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
            if (*s == 0 && s + 1 < end && s[1] == 0)
            {
                for (n = 1; s + n < end && s[n] == 0; ++n)
                    ;
                os << '+' << n << 'z';
                s += n;
                continue;
            }

            os << uint2hex(*s / 16) << uint2hex(*s % 16);
            for (n = 1; s + n < end && s[n] == s[0]; ++n)
                ;
            if (n >= 2)
            {
                os << '+' << n-1 << 'r';
                s += n - 1;
            }
            ++s;
        }
    }

    static
    void LoadBinaryData(void *buf, size_t max, istream& is)
    {
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
            // whitespace: ignore
            if (::isspace(c))
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
                throw exceptf<>("Invalid character in binary input: %c", c);
        }
    }

    static
    bool
    PrintValue(ostream& os, SampleVariableDataType t, size_t w, const void *p)
    {
        switch(t) {
        case SV_BINARY:
            os << dec << w << ' ';
            DumpBinaryData(os, p, w);
            return true;
        case SV_BOOL:
            os << boolalpha << *(const bool*)p;
            return true;
        case SV_INTEGER:
            os << "0x" << hex;
            switch(w) {
            case 1: os << (unsigned)*(const uint8_t*)p; break;
            case 2: os << *(const uint16_t*)p; break;
            case 4: os << *(const uint32_t*)p; break;
            case 8: os << *(const uint64_t*)p; break;
            default: os << "<invsize>"; break;
            }
            return true;
        case SV_FLOAT:
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
                    snprintf(buf, 50, "<invsize>");
                    break;
                }
                os << buf;
            }
            return true;
        default:
            UNREACHABLE;
            break;
        }
        return false;
    }

    static
    void WriteValue(SampleVariableDataType dt, void *var, size_t width, istream& is)
    {
        switch(dt)
        {
        case SV_BINARY:
        {
            size_t n;
            is >> n;
            if (n != width)
                throw exceptf<>("Invalid binary blob: expected size %zu, got %zu", width, n);
            LoadBinaryData(var, n, is);
        }
        break;
        case SV_BOOL:
        {
            string s;
            is >> s;
            transform(s.begin(), s.end(), s.begin(), ::tolower);
            if (s == "yes" || s == "true" || s == "1")
                *(bool*)var = true;
            else if (s == "no" || s == "false" || s == "0")
                *(bool*)var = false;
            else
                throw exceptf<>("Invalid boolean value: %s", s.c_str());
        }
        break;
        case SV_INTEGER:
        {
            string val;
            is >> val;
            if (val.size() > 0 && val[0] == '-')
                switch(width)
                {
                case 1: *(int8_t*)var = (int8_t)stoll(val, 0, 0); break;
                case 2: *(int16_t*)var = (int16_t)stoll(val, 0, 0); break;
                case 4: *(int32_t*)var = (int32_t)stoll(val, 0, 0); break;
                case 8: *(int64_t*)var = (int64_t)stoll(val, 0, 0); break;
                default: throw exceptf<>("Can't convert more than 64 bits");
                }
            else
                switch(width)
                {
                case 1: *(uint8_t*)var = (uint8_t)stoull(val, 0, 0); break;
                case 2: *(uint16_t*)var = (uint16_t)stoull(val, 0, 0); break;
                case 4: *(uint32_t*)var = (uint32_t)stoull(val, 0, 0); break;
                case 8: *(uint64_t*)var = (uint64_t)stoull(val, 0, 0); break;
                default: throw exceptf<>("Can't convert more than 64 bits");
                }
        }
        break;
        case SV_FLOAT:
        {
            string val;
            is >> val;
            switch(width)
            {
            case sizeof(float): *(float*)var = (float)stof(val, 0); break;
            case sizeof(double): *(double*)var = (double)stod(val, 0); break;
            case sizeof(long double): *(long double*)var = (long double)stold(val, 0); break;
            default: throw exceptf<>("Can't convert unknown width float");
            }
        }
        break;
        default:
            UNREACHABLE;
            break;
        }
    }


    Serializer& Serializer::operator&(const char *tag)
    {
        if (reading)
            *os << ' ' << tag;
        else
        {
            string s;
            *is >> s;
            if (s != tag)
                throw exceptf<>("Invalid serialized data: expected tag %s, got %s", tag, s.c_str());
        }
        return *this;
    }

    void Serializer::serialize_raw(SampleVariableDataType dt, void* var, size_t sz)
    {
        if (reading)
        {
            *os << ' ';
            PrintValue(*os, dt, sz, var);
        }
        else
            WriteValue(dt, var, sz, *is);
    }


    static
    void ListSampleVariables_onevar(ostream& os, const string& name, const VarInfo& vinfo)
    {
        os << dec << vinfo.width << "\t";

        switch(vinfo.cat)
        {
        case SVC_LEVEL: os << "level"; break;
        case SVC_STATE: os << "state"; break;
        case SVC_WATERMARK: os << "wmark"; break;
        case SVC_CUMULATIVE: os << "cumul"; break;
        default: os << "unknown"; break;
        }

        switch(vinfo.type)
        {
        case SV_BOOL: os << "\tbool\t"; break;
        case SV_INTEGER: os << "\tint\t"; break;
        case SV_FLOAT: os << "\tfloat\t"; break;
        case SV_BINARY: os << "\tbinary\t"; break;
        case SV_OTHER: os << "\tother\t"; break;
        }

        if (vinfo.max.size() > 0)
        {
            const void *p = &vinfo.max[0];
            if (!PrintValue(os, vinfo.type, vinfo.width, p))
                os << "N/A";
        }
        else
            os << "N/A";

        os << '\t' << vinfo.var << '\t'
           << name
           << endl;
    }

    static
    void ListSampleVariables_header(ostream& os)
    {
        os << "# size\ttype\tdtype\tmax\taddress\tname" << endl;
    }

    void ListSampleVariables(ostream& os, const string& pat)
    {
        ListSampleVariables_header(os);
        for (auto& i : GetRegistry())
        {
            if (FNM_NOMATCH == fnmatch(pat.c_str(), i.first.c_str(), 0))
                continue;
            ListSampleVariables_onevar(os, i.first, i.second);
        }
    }


    void SetSampleVariables(ostream& os, const string& pat, const string& val)
    {
        for (auto& i : GetRegistry())
        {
            if (FNM_NOMATCH == fnmatch(pat.c_str(), i.first.c_str(), 0))
                continue;
            os << "Writing " << i.first << "..." << std::endl;
            istringstream is(val);
            switch(i.second.type)
            {
            case SV_BINARY:
                WriteValue(SV_BINARY, i.second.var, i.second.width, is);
                break;
            default:
            {
                Serializer s;
                s.reading = false; s.is = &is;
                i.second.ser(s, i.second.var);
            }
            }
        }
    }


    bool ReadSampleVariables(ostream& os, const string& pat)
    {
        bool some = false;
        for (auto& i : GetRegistry())
        {
            if (FNM_NOMATCH == fnmatch(pat.c_str(), i.first.c_str(), 0))
                continue;

            os << i.first << " =";

            const VarInfo& vinfo = i.second;
            switch(vinfo.type)
            {
            case SV_BINARY:
                os << ' ';
                PrintValue(os, vinfo.type, vinfo.width, vinfo.var);
                break;
            default:
            {
                Serializer s;
                s.reading = true; s.os = &os;
                vinfo.ser(s, vinfo.var);
            }
            }
            os << endl;
            some = true;
        }
        return some;
    }

    typedef pair<const string*, const VarInfo*> varsel_t;
    typedef vector<varsel_t> varvec_t;

    BinarySampler::BinarySampler(ostream& os, const Config& config,
                                 const vector<string>& pats)
        : m_datasize(0), m_vars()
    {
        varvec_t vars;

        //
        // Select variables to sample
        //
        auto& registry = GetRegistry();
        for (auto& i : pats)
            for (auto& j : registry)
            {
                if (FNM_NOMATCH == fnmatch(i.c_str(), j.first.c_str(), 0))
                    continue;

                if (j.second.type == SV_OTHER)
                    throw exceptf<>("Cannot monitor the non-scalar variable %s (selected by %s)", j.first.c_str(), i.c_str());

                vars.push_back(make_pair(&j.first, &j.second));
            }

        if (vars.size() >= 2)
            // we sort everything but the first and last variables,
            // which should be the cycle counters. The cycle counters
            // must be sampled once before and after everything else,
            // to evaluate how imprecise the measurement is.
            sort(vars.begin()+1, vars.end()-1,
                 [](const varsel_t& left, const varsel_t& right) -> bool
                 { return left.second->var < right.second->var; });

        //
        // Generate header for output file
        //
        time_t cl = time(0);
        string timestr = asctime(gmtime(&cl));

        os << "# date: " << timestr // asctime already embeds a newline character
           << "# generator: " << PACKAGE_STRING << endl;

        char hn[255];
        if (gethostname(hn, 255) == 0)
            os << "# host: " << hn << endl;

        vector<pair<string, string> > rawconf = config.getRawConfiguration();
        os << "# configuration: " << rawconf.size() << endl;
        for (auto& i : rawconf)
            os << i.first << " = " << i.second << endl;

        os << "# varinfo: " << vars.size() << endl;
        for (auto& i : vars)
        {
            m_datasize += i.second->width;
            m_vars.push_back(make_pair((const char*)i.second->var, i.second->width));
            ListSampleVariables_onevar(os, *i.first, *i.second);
        }
        os << "# recwidth: " << m_datasize << endl;
    }

}
