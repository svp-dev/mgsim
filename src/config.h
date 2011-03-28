#ifndef CONFIG_H
#define CONFIG_H

#include <algorithm>
#include <sstream>
#include <map>
#include <string>
#include <vector>
#include <ostream>

class Config
{
    typedef std::map<std::string,std::string> ConfigMap;
    
    ConfigMap        m_data;
    const ConfigMap& m_overrides;

public:
    
    template <typename T>
    T getValue(const std::string& name, const T& def) const
    {
        T val;
        std::stringstream stream;
        stream << getValue<std::string>(name, "");
        stream >> val;
        return (!stream.fail() && stream.eof()) ? val : def;
    }

    template <typename T>
    std::vector<T> getValueList(const std::string& name) const
    {
        std::vector<T> vals;
        std::string str = getValue<std::string>(name,"");
        std::istringstream stream(str);
        std::string token = "";
        while (getline(stream, token, ','))
        {
            std::stringstream ss;
            T val;
            ss << token;
            ss >> val;
            if (ss.fail()) break;
            vals.push_back(val);
        }
        return vals;
    }

    template <typename T>
    T getSize(const std::string& name, const T& def) const
    {
        T val;
        std::string strmult;
        std::stringstream stream;
        stream << getValue<std::string>(name, "");
        stream >> val;
        stream >> strmult;
        if (!strmult.empty())
        {
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
        }
        return (!stream.fail() && stream.eof()) ? val : def;
    }

    void dumpConfiguration(std::ostream& os, const std::string& cf) const;

    Config(const std::string& filename, const ConfigMap& overrides);
};

template <>
bool Config::getValue<bool>(const std::string& name, const bool& def) const;

template <>
std::string Config::getValue<std::string>(const std::string& name, const std::string& def) const;

#endif
