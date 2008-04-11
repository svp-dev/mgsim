#ifndef CLIB_EXCEPT_H
#define CLIB_EXCEPT_H

#include <string>

namespace Simulator
{

// Base exception class
class Exception
{
    const std::string message;
public:
    virtual Exception*  clone()   { return new Exception( *this ); }
    virtual void        raise()   { throw Exception( *this ); }
    virtual const char* getType() { return "Exception"; }

    const std::string& getMessage()
    {
        return message;
    }

    Exception( const std::string& msg ) : message(msg) {}
    Exception() {}
    virtual ~Exception() {}
};

#define EXCEPTION_COMMON(name) 
/*\
    Exception*  clone()   { return new name( *this ); } \
    void        raise()   { throw name( *this ); } \
    const char* getType() { return #name; }
*/
class IllegalPortAccess : public Exception
{
public:
    EXCEPTION_COMMON(IllegalPortAccess)

    IllegalPortAccess(const std::string& componentName) : Exception(componentName + " tried to access a port illegally") {}
};

class InvalidArgumentException : public Exception
{
public:
    EXCEPTION_COMMON(InvalidArgumentException)

    InvalidArgumentException( const std::string& msg ) : Exception(msg) {}
    InvalidArgumentException() {}
};

class IllegalInstructionException : public Exception
{
public:
    EXCEPTION_COMMON(IllegalInstructionException)

    IllegalInstructionException( const std::string& msg ) : Exception(msg) {}
    IllegalInstructionException() {}
};

class IOException : public Exception
{
public:
    EXCEPTION_COMMON(IOException)

    IOException( const std::string& msg ) : Exception(msg) {}
    IOException() {}
};

class FileNotFoundException : public IOException
{
public:
    EXCEPTION_COMMON(FileNotFoundException)

    FileNotFoundException( const std::string& filename ) : IOException("File not found: " + filename) {}
    FileNotFoundException() {}
};

class SecurityException : public Exception
{
public:
    EXCEPTION_COMMON(SecurityException)

    SecurityException( const std::string& msg) : Exception(msg) {}
    SecurityException() {}
};

}
#endif

