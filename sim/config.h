#ifndef CONFIG_H
#define CONFIG_H

#include <algorithm>
#include <sstream>
#include <map>
#include <string>
#include <vector>
#include <ostream>
#include <cerrno>
#include <cstring>
#include <inttypes.h>
#include "kernel.h"

class Config
{
    // a sequence of (pattern, value) pairs 
    typedef std::vector<std::pair<std::string,std::string> > ConfigMap;
    
    // a cache of (key, (value, origin pattern)) pairs
    typedef std::map<std::string,std::pair<std::string,std::string> > ConfigCache;
    
    ConfigMap         m_data;
    const ConfigMap&  m_overrides;
    ConfigCache       m_cache;

public:

    template <typename T>
    T getValue(const std::string& name, const T& def) 
    {
        /* general version for numbers. The version for strings and bools is specialized below. */

        // convert the default value to string
        std::stringstream defv; 
        defv << def;
        // get the string value back
        std::string s = getValue<std::string>(name, defv.str());
        const char *start = s.c_str();
        char *end;
        T val;

        val = strtoumax(start, &end, 0);
        if (errno == EINVAL)
        {
            val = strtoimax(start, &end, 0);
            if (errno == EINVAL)
                return def;
        }
        if (*end != '\0')
        {
            // a prefix is specified, maybe a multiplier?
            if (strcmp(end, "GiB"))
                val *= 1024ULL*1024*1024;
            else if (strcmp(end, "GB"))
                val *= 1000000000ULL;
            else if (strcmp(end, "MiB"))
                val *= 1024*1024;
            else if (strcmp(end, "MB"))
                val *= 1000000;
            else if (strcmp(end, "KiB"))
                val *= 1024;
            else if (strcmp(end, "KB"))
                val *= 1000;
            else
                return def;
        }
        return val;
    }

    template <typename T>
    T getValue(const Simulator::Object& obj, const std::string& name, const T& def)
    {
        return getValue(obj.GetFQN() + '.' + name, def);
    }

    std::vector<std::string> getWordList(const std::string& name); 
    std::vector<std::string> getWordList(const Simulator::Object& obj, const std::string& name);

    template <typename T>
    T getSize(const std::string& name, const T& def)
    {
        T val;
        std::stringstream defv;
        defv << def;
        std::string strmult;
        std::stringstream stream;
        stream << getValue<std::string>(name, defv.str());
        stream >> val;
        if (stream.fail())
            return def;
        if (stream.eof())
            return val;
        stream >> strmult;
        if (strmult.size() == 2)
        {
            switch (toupper(strmult[0]))
            {
            default: return def;
            case 'G': val = val * 1024;
            case 'M': val = val * 1024;
            case 'K': val = val * 1024;
            }                
            strmult = strmult.substr(1);
        }
        
        if (strmult.size() != 1 || toupper(strmult[0]) != 'B')
        {
            return def;
        }
        return (!stream.fail() && stream.eof()) ? val : def;
    }

    template <typename T>
    T getSize(const Simulator::Object& obj, const std::string& name, const T& def)
    {
        return getSize(obj.GetFQN() + '.' + name, def);
    }

    void dumpConfiguration(std::ostream& os, const std::string& cf) const;

    Config(const std::string& filename, const ConfigMap& overrides);
};

template <>
bool Config::getValue<bool>(const std::string& name, const bool& def);

template <>
std::string Config::getValue<std::string>(const std::string& name, const std::string& def);

#endif
