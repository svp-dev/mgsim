#include "configparser.h"
#include "except.h"
#include <algorithm>
#include <cctype>
#include <cstring>

using namespace std;
using namespace Simulator;

#define NEXTLINE() do { while (*m_lookahead && *m_lookahead != '\n') ++m_lookahead; } while(0)
#define WHITESPACE() do { while (*m_lookahead && (*m_lookahead == ' ' || *m_lookahead == '\t')) ++m_lookahead; } while(0)
#define MLWHITESPACE() do { while (*m_lookahead && isspace(*m_lookahead)) { if (*m_lookahead == '\n') ++m_linenr; ++m_lookahead; } } while(0)
#define EXPECT(Char)                                                    \
    do {                                                                \
        if (*m_lookahead != Char)                                       \
            throw exceptf<runtime_error>("Syntax error in configuration: line %d, found '%c' (%d), expected '%c' (%d)", \
                                         m_linenr, isprint(*m_lookahead) ? *m_lookahead : '?', (int)*m_lookahead, isprint(Char) ? Char : '?', (int)Char); \
    } while (0)
#define MATCH(Char) do { EXPECT(Char); ++m_lookahead; } while(0)

void ConfigParser::parse_sectionstr()
{
    m_prefix.clear();
    MATCH('[');
    while (*m_lookahead && !strchr("]\n", *m_lookahead))
        m_prefix += *m_lookahead++;
    MATCH(']');
    transform(m_prefix.begin(), m_prefix.end(), m_prefix.begin(), ::tolower);
}

void ConfigParser::parse_keystr()
{
    while (*m_lookahead && (isalnum(*m_lookahead) || strchr("_*.:", *m_lookahead)))
        m_key += *m_lookahead++;
}

void ConfigParser::parse_valuestr()
{
    while (!strchr("\n#;", *m_lookahead))
        m_value += *m_lookahead++;

    if (*m_lookahead && strchr("#;", *m_lookahead))
        NEXTLINE();
}

void ConfigParser::parse_keyvalue()
{
    m_key.clear(); m_value.clear();

    parse_keystr();
    WHITESPACE(); MATCH('='); WHITESPACE();
    if (m_key.empty())
        throw exceptf<runtime_error>("Empty key found before '=' in configuration around line %d", m_linenr);
    parse_valuestr();
        
    // Strip off all the spaces from the end
    string::size_type pos = m_value.find_last_not_of("\r\n\t\v\f ");
    if (pos != string::npos)
        m_value.erase(pos + 1);
    
    // Concatenate key and prefix
    if (!m_prefix.empty() && m_prefix != "global")
    {
        if (m_key[0] != ':')
            m_key = m_prefix + '.' + m_key;
        else
            m_key = m_prefix + m_key;
    }

    // Store key/value pair
    m_data.append(m_key, m_value);
}

void ConfigParser::parse_item()
{
    if (*m_lookahead && *m_lookahead == '[')
        parse_sectionstr();
    else
        parse_keyvalue();
}


void ConfigParser::operator()(const string &inputdata)
{
    m_lookahead = inputdata.c_str();
    m_linenr = 1;
    string prefix;
    while (*m_lookahead)
    {
        if (isspace(*m_lookahead))
            MLWHITESPACE();
        else if (*m_lookahead && strchr("#;", *m_lookahead))
            NEXTLINE();
        else
            parse_item();
    }
    EXPECT('\0');
}
