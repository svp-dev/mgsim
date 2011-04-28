#include "sampling.h"
#include "config.h"
#include "sys_config.h"
#include "arch/MGSystem.h"

#include <algorithm>
#include <map>
#include <vector>
#include <ctime>
#include <fnmatch.h>
#include <unistd.h>

struct VarInfo 
{
    void *                 var;
    size_t                 width;
    SampleVariableDataType type;
    SampleVariableCategory cat;
    std::vector<char>      max;

    VarInfo() {};
};

typedef std::map<std::string, VarInfo> var_registry_t;

static
var_registry_t registry;

void _RegisterSampleVariable(void *var, size_t width, const std::string& name, SampleVariableDataType type, SampleVariableCategory cat, void *maxval)
{
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
void ListSampleVariables_header(std::ostream& os)
{
    os << "# size\ttype\tdtype\tmax\taddress\tname" << std::endl;
}

static
void ListSampleVariables_onevar(std::ostream& os, const std::string& name, const VarInfo& vinfo)
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
            case 1: os << std::dec << *(uint8_t*)p; break;
            case 2: os << std::dec << *(uint16_t*)p; break;
            case 4: os << std::dec << *(uint32_t*)p; break;
            case 8: os << std::dec << *(uint64_t*)p; break;
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
       << std::endl;
}

void ListSampleVariables(std::ostream& os, const std::string& pat)
{
    ListSampleVariables_header(os);
    for (var_registry_t::const_iterator i = registry.begin();
         i != registry.end();
         ++i)
    {
        if (FNM_NOMATCH == fnmatch(pat.c_str(), i->first.c_str(), FNM_CASEFOLD))
            continue;
        ListSampleVariables_onevar(os, i->first, i->second);
    }
}

void ReadSampleVariables(std::ostream& os, const std::string& pat)
{
    for (var_registry_t::const_iterator i = registry.begin();
         i != registry.end();
         ++i)
    {
        if (FNM_NOMATCH == fnmatch(pat.c_str(), i->first.c_str(), FNM_CASEFOLD))
            continue;

        os << i->first << " = ";

        const VarInfo& vinfo = i->second;
        void *p = vinfo.var;
        switch(vinfo.type) {
        case SV_INTEGER:
            switch(vinfo.width) {
            case 1: os << std::dec << *(uint8_t*)p; break;
            case 2: os << std::dec << *(uint16_t*)p; break;
            case 4: os << std::dec << *(uint32_t*)p; break;
            case 8: os << std::dec << *(uint64_t*)p; break;
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
        os << std::endl;        
    }
}

typedef std::pair<const std::string*, const VarInfo*> varsel_t;
typedef std::vector<varsel_t> varvec_t;

static
bool comparevars(const varsel_t& left, const varsel_t& right)
{
    return left.second->var < right.second->var;
}

BinarySampler::BinarySampler(std::ostream& os, const Simulator::MGSystem& sys,
                             const std::vector<std::string>& pats)
    : m_datasize(0)
{

    varvec_t vars;

    //
    // Select variables to sample
    //

    for (std::vector<std::string>::const_iterator i = pats.begin(); i != pats.end(); ++i)
        for (var_registry_t::const_iterator j = registry.begin();
             j != registry.end();
             ++j)
    {
        if (FNM_NOMATCH == fnmatch(i->c_str(), j->first.c_str(), FNM_CASEFOLD))
            continue;
        vars.push_back(std::make_pair(&j->first, &j->second));
    }

    if (vars.size() >= 2)
        // we sort everything but the first and last variables,
        // which should be the cycle counters. The cycle counters
        // must be sampled once before and after everything else,
        // to evaluate how imprecise the measurement is.
        std::sort(vars.begin()+1, vars.end()-1, comparevars);

    //
    // Generate header for output file
    // 
    time_t cl = time(0);
    std::string timestr = asctime(gmtime(&cl));

    os << "# date: " << timestr // asctime already embeds a newline character
       << "# generator: " << PACKAGE_STRING << std::endl;

    char hn[255];
    if (gethostname(hn, 255) == 0)
        os << "# host: " << hn << std::endl;
    
    os << "# program:" << std::endl;
    const std::string& prog = sys.GetProgramName();
    const std::vector<std::string> &inputs = sys.GetInputFileNames();
    os << prog << std::endl
       << "# inputs: " << inputs.size() << std::endl;
    for (std::vector<std::string>::const_iterator i = inputs.begin();
         i != inputs.end(); ++i)
        os << *i << std::endl;

    Simulator::MGSystem::ConfWords words;
    sys.FillConfWords(words);
    os << "# confwords: " << words.data.size() << std::endl;
    for (std::vector<uint32_t>::const_iterator i = words.data.begin();
         i != words.data.end(); ++i)
    {
        os << *i << ' ';
    }
    os << std::endl;

    os << "# varinfo: " << vars.size() << std::endl;
    // ListSampleVariables_header(os);
    for (varvec_t::const_iterator i = vars.begin(); i != vars.end(); ++i)
    {
        m_datasize += i->second->width;
        m_vars.push_back(std::make_pair((const char*)i->second->var, i->second->width));
        ListSampleVariables_onevar(os, *i->first, *i->second);
    }
    os << "# recwidth: " << m_datasize << std::endl;
}
