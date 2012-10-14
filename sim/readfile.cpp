#include "readfile.h"
#include <stdexcept>
#include <sstream>
#include <cstdio>
#include <cerrno>
#include <cstring>

using namespace std;

/* read_file: read the entire contents of a file into a C++ string. */

string read_file(const string& filename)
{
    // Open the file
    int errno_;
    FILE* fd;
    if (filename == "-")
    {
        fd = stdin;
    }
    else if (NULL == (fd = fopen(filename.c_str(), "r")))
    {
        errno_ = errno;
        throw runtime_error(string("fopen: ") + strerror(errno_));
    }

    // Read the data onto a string
    char *buf = new char[BUFSIZ];
    string data;
    while (!feof(fd))
    {
        size_t sz = fread(buf, 1, BUFSIZ, fd);
        if (ferror(fd))
        {
            errno_ = errno;
            break;
        }
        data.append(buf, sz);
    }
    delete[] buf;

    // Check for errors / close file
    ostringstream errmsg;
    bool error = false;
    if (ferror(fd))
    {
        error = true;
        errmsg << "fread: " << strerror(errno_);
    }
    if (filename != "-" && fclose(fd))
    {
        error = true;
        errno_ = errno;
        if (error) errmsg << "; ";
        errmsg << "fclose: " << strerror(errno_);
    }
    if (error)
        throw runtime_error(errmsg.str());

    // Return data
    return data;
}

