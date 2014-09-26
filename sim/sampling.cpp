#include "sampling.h"
#include "config.h"
#include <sys_config.h>
#include <arch/MGSystem.h>

#include <algorithm>
#include <map>
#include <vector>
#include <ctime>
#include <fnmatch.h>
#include <unistd.h>

using namespace std;

struct VarInfo
{
    void *                 var;
    size_t                 width;
    SampleVariableDataType type;
    SampleVariableCategory cat;
    vector<char>           max;

    VarInfo() : var(0), width(0), type(SV_INTEGER), cat(SVC_LEVEL), max() {};
    VarInfo(const VarInfo&) = default;
    VarInfo& operator=(const VarInfo&) = default;
};

typedef map<string, VarInfo> var_registry_t;

static var_registry_t& GetRegistry()
{
    static var_registry_t registry;
    return registry;
}

void _RegisterSampleVariable(void *var, size_t width, const string& name, SampleVariableDataType type, SampleVariableCategory cat, void *maxval)
{
    auto &registry = GetRegistry();
    assert (registry.find(name) == registry.end()); // no duplicates allowed.

    VarInfo vinfo;

    vinfo.var = var;
    vinfo.width = width;
    vinfo.type = type;
    vinfo.cat = cat;

    const char *maxdata = (const char*)maxval;
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
void ListSampleVariables_onevar(ostream& os, const string& name, const VarInfo& vinfo)
{
    os << vinfo.width << "\t";

    switch(vinfo.cat)
    {
    case SVC_LEVEL: os << "level"; break;
    case SVC_STATE: os << "state"; break;
    case SVC_WATERMARK: os << "wmark"; break;
    case SVC_CUMULATIVE: os << "cumul"; break;
    default: os << "unknown"; break;
    }
    os << (const char*)((vinfo.type == SV_INTEGER) ? "\tint\t" : "\tfloat\t");

    if (vinfo.cat == SVC_LEVEL || vinfo.cat == SVC_WATERMARK)
    {
        const void *p = &vinfo.max[0];
        switch(vinfo.type) {
        case SV_INTEGER:
            switch(vinfo.width) {
            case 1: os << dec << (unsigned)*(uint8_t*)p; break;
            case 2: os << dec << *(uint16_t*)p; break;
            case 4: os << dec << *(uint32_t*)p; break;
            case 8: os << dec << *(uint64_t*)p; break;
            default: os << "<invsize>"; break;
            }
            break;
        case SV_FLOAT:
            if (vinfo.width == sizeof(float))
                os << *(float*)p;
            else
                os << *(double*)p;
            break;
        }
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
        switch(vinfo.type) {
        case SV_INTEGER:
            switch(vinfo.width) {
            case 1: os << dec << (unsigned)*(uint8_t*)p; break;
            case 2: os << dec << *(uint16_t*)p; break;
            case 4: os << dec << *(uint32_t*)p; break;
            case 8: os << dec << *(uint64_t*)p; break;
            default: os << "<invsize>"; break;
            }
            break;
        case SV_FLOAT:
            if (vinfo.width == sizeof(float))
                os << *(float*)p;
            else
                os << *(double*)p;
            break;
        }
        os << endl;
        some = true;
    }
    return some;
}

typedef pair<const string*, const VarInfo*> varsel_t;
typedef vector<varsel_t> varvec_t;

static
bool comparevars(const varsel_t& left, const varsel_t& right)
{
    return left.second->var < right.second->var;
}

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
            vars.push_back(make_pair(&j.first, &j.second));
        }

    if (vars.size() >= 2)
        // we sort everything but the first and last variables,
        // which should be the cycle counters. The cycle counters
        // must be sampled once before and after everything else,
        // to evaluate how imprecise the measurement is.
        sort(vars.begin()+1, vars.end()-1, comparevars);

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
    // ListSampleVariables_header(os);
    for (auto& i : vars)
    {
        m_datasize += i.second->width;
        m_vars.push_back(make_pair((const char*)i.second->var, i.second->width));
        ListSampleVariables_onevar(os, *i.first, *i.second);
    }
    os << "# recwidth: " << m_datasize << endl;
}
