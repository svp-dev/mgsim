// -*- c++ -*-
#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include "config.h"

/* ConfigParser: parse the .INI format with extensions into a ConfigMap. */

class ConfigParser
{
private:
    const char* m_lookahead;
    int m_linenr;
    ConfigMap& m_data;
    std::string m_prefix;
    std::string m_key;
    std::string m_value;

public:
    ConfigParser(ConfigMap& outputdata)
        : m_lookahead(NULL),
          m_linenr(0),
          m_data(outputdata),
          m_prefix(), m_key(), m_value()
    {}
    ConfigParser(const ConfigParser&) = delete;
    ConfigParser& operator=(const ConfigParser&) = delete;

private:

    void parse_sectionstr();
    void parse_keystr();
    void parse_valuestr();
    void parse_keyvalue();
    void parse_item();

public:
    void operator()(const std::string& data);
};


#endif
