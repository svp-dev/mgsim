#include "sim/sampling.h"
#include "sim/config.h"
#include "sim/except.h"
#include <sys_config.h>
#include <arch/MGSystem.h>

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

    struct VarInfo
    {
        SampleVariableDataType type;
        SampleVariableCategory cat;
        void *                 var;
        size_t                 width;
        vector<char>           max;

        VarInfo() : type(SV_INTEGER), cat(SVC_LEVEL), var(0), width(0), max() {};
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
                                size_t width, void *maxval)
    {
        auto &registry = GetRegistry();
        if (registry.find(name) != registry.end())
            throw Simulator::exceptf<>("Duplicate variable registration: %s", name.c_str());

        VarInfo vinfo;

        vinfo.var = var;
        vinfo.width = width;
        vinfo.type = type;
        vinfo.cat = cat;

        const char *maxdata = (const char*)maxval;
        if (maxdata)
            for (size_t i = 0; i < width; ++i)
                vinfo.max.push_back(maxdata[i]);

        registry[name] = vinfo;
    }

    static
    void ListSampleVariables_header(ostream& os)
    {
        os << "# size\ttype\tdtype\tmax\taddress\tname" << endl;
    }

    static
    bool
    PrintValue(ostream& os, SampleVariableDataType t, size_t w, const void *p)
    {
        switch(t) {
        case SV_BINARY:
            os << dec << w << ' ' << hex;
            for (size_t i = 0; i < w; ++i)
                os << ((((unsigned)*(const char*)p)>>4)&0xf)
                   << ((((unsigned)*(const char*)p)   )&0xf);
            os << dec;
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
            break;
        }
        return false;
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
        case SV_CUSTOM: os << "\tother\t"; break;
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

    bool ReadSampleVariables(ostream& os, const string& pat)
    {
        bool some = false;
        for (auto& i : GetRegistry())
        {
            if (FNM_NOMATCH == fnmatch(pat.c_str(), i.first.c_str(), 0))
                continue;

            os << i.first << " = ";

            const VarInfo& vinfo = i.second;
            void *p = vinfo.var;
            switch(vinfo.type)
            {
            case SV_CUSTOM:
                os << "<FIXME:Custom>";
                break;
            default:
                PrintValue(os, vinfo.type, vinfo.width, p);
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

                if (j.second.type == SV_CUSTOM)
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
