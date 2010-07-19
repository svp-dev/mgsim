#include "sampling.h"

#include <map>
#include <vector>
#include <fnmatch.h>

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
    assert (registry.find(name) != registry.end()); // no duplicates allowed.

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


void ListSampleVariables(std::ostream& os, const std::string& pat)
{
    os << "# size\ttype\tdtype\taddress\tname" << std::endl;
    for (var_registry_t::const_iterator i = registry.begin();
         i != registry.end();
         ++i)
    {
        if (FNM_NOMATCH == fnmatch(pat.c_str(), i->first.c_str(), FNM_CASEFOLD))
            continue;
        const VarInfo& vinfo = i->second;

        
        os << vinfo.width << "\t";

        switch(vinfo.cat) {
        case SVC_LEVEL: os << "level"; break;
        case SVC_STATE: os << "state"; break;
        case SVC_WATERMARK: os << "wmark"; break;
        case SVC_CUMULATIVE: os << "cumul"; break;
        default: os << "unknown"; break;
        }
        os << (const char*)((vinfo.type == SV_INTEGER) ? "\tint\t" : "\tfloat\t")
           << vinfo.var << '\t'
           << i->first
           << std::endl;        
    }
}

void ShowSampleVariables(std::ostream& os, const std::string& pat)
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
            default: os << "<unknown size, cannot print>"; break;
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
