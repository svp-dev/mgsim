#ifndef CLIB_EXCEPT_H
#define CLIB_EXCEPT_H

#include <string>
#include <stdexcept>

namespace Simulator
{

class Object;

// Base exception class
class SimulationException : public std::runtime_error
{
public:
    SimulationException(const std::string& msg) : std::runtime_error(msg) {}
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
    SecurityException(const Object& , const std::string& msg) : SimulationException(msg) {}
};

}
#endif

