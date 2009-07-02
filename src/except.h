#ifndef CLIB_EXCEPT_H
#define CLIB_EXCEPT_H

#include <string>
#include <stdexcept>
#include <list>

namespace Simulator
{

class Object;

// Base exception class
class SimulationException : public std::runtime_error
{
    std::list<std::string> m_details;
public:
    const std::list<std::string>& GetDetails() const { return m_details; }
    
    void AddDetails(const std::string& msg) { m_details.push_back(msg); }
    SimulationException(const std::string& msg) : std::runtime_error(msg) {}
    SimulationException(const std::string& msg, const Object& object);
    virtual ~SimulationException() throw() {}
};

class InvalidArgumentException : public std::runtime_error
{
public:
    InvalidArgumentException(const std::string& msg) : std::runtime_error(msg) {}
};

class IllegalInstructionException : public SimulationException
{
public:
    IllegalInstructionException(const Object& , const std::string& msg) : SimulationException(msg) {}
};

class IOException : public std::runtime_error
{
public:
    IOException(const std::string& msg) : std::runtime_error(msg) {}
};

class FileNotFoundException : public IOException
{
public:
    FileNotFoundException(const std::string& filename) : IOException("File not found: " + filename) {}
};

class SecurityException : public SimulationException
{
public:
    SecurityException(const std::string& msg, const Object& object) : SimulationException(msg, object) {}
};

}
#endif

