#include <sys_config.h>
#include <sim/sampling.h>
#include <sim/except.h>

#include <sstream>
#include <fnmatch.h>

using namespace std;

namespace Simulator
{
    VariableRegistry::VariableRegistry()
        : m_registry()
    {}

    void VariableRegistry::RegisterVariable(void *var, const string& name,
                                            VariableCategory cat,
                                            ValueType type,
                                            size_t width, void *maxval,
                                            serializer_func_t ser)
    {
        if (m_registry.find(name) != m_registry.end())
            throw exceptf<>("Duplicate variable registration: %s",
                            name.c_str());

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

        m_registry[name] = vinfo;
    }

    void VariableRegistry::ListVariables_onevar(ostream& os,
                                                const string& name,
                                                const VarInfo& vinfo)
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
        case Serialization::SV_BOOL: os << "\tbool\t"; break;
        case Serialization::SV_INTEGER: os << "\tint\t"; break;
        case Serialization::SV_FLOAT: os << "\tfloat\t"; break;
        case Serialization::SV_BINARY: os << "\tbytes\t"; break;
        case Serialization::SV_BITS: os << "\tbits\t"; break;
        case Serialization::SV_OTHER: os << "\tother\t"; break;
        }

        if (vinfo.max.size() > 0)
        {
            const void *p = &vinfo.max[0];
            StreamSerializer s(os, false);
            s.serialize_raw_ro(vinfo.type, p, vinfo.width);
        }
        else
            os << "N/A";

        os << '\t' << vinfo.var << '\t'
           << name
           << endl;
    }

    void VariableRegistry::ListVariables_header(ostream& os)
    {
        os << "# size\ttype\tdtype\tmax\taddress\tname" << endl;
    }

    void VariableRegistry::ListVariables(ostream& os, const string& pat) const
    {
        ListVariables_header(os);
        for (auto& i : GetRegistry())
        {
            if (FNM_NOMATCH == fnmatch(pat.c_str(), i.first.c_str(), 0))
                continue;
            ListVariables_onevar(os, i.first, i.second);
        }
    }


    void VariableRegistry::SetVariables(ostream& os, const string& pat,
                                        const string& val) const
    {
        for (auto& i : m_registry)
        {
            if (FNM_NOMATCH == fnmatch(pat.c_str(), i.first.c_str(), 0))
                continue;
            os << "Writing " << i.first << "..." << std::endl;

            istringstream is(val);
            StreamSerializer s(is);

            switch(i.second.type)
            {
            case Serialization::SV_BITS:
            case Serialization::SV_BINARY:
                s.serialize_raw(i.second.type, i.second.var, i.second.width);
                break;
            default:
                i.second.ser(s, i.second.var);
                break;
            }
        }
    }


    bool VariableRegistry::RenderVariables(ostream& os, const string& pat,
                                           bool compact) const
    {
        bool some = false;
        for (auto& i : m_registry)
        {
            if (FNM_NOMATCH == fnmatch(pat.c_str(), i.first.c_str(), 0))
                continue;

            os << i.first << " =";

            StreamSerializer s(os, compact);

            const VarInfo& vinfo = i.second;
            switch(vinfo.type)
            {
            case Serialization::SV_BITS:
            case Serialization::SV_BINARY:
                s.serialize_raw(vinfo.type, vinfo.var, vinfo.width);
                break;
            default:
                vinfo.ser(s, vinfo.var);
            }
            os << endl;
            some = true;
        }
        return some;
    }


}
