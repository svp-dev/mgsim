#include "arch/Memory.h"
#include <iomanip>

using namespace std;

namespace Simulator
{

IMemoryAdmin::~IMemoryAdmin()
{}


void IMemoryAdmin::Cmd_Read(ostream& out, const vector<string>& arguments) const
{
    MemAddr addr = 0;
    MemSize size = 0;
    char* endptr = NULL;

    if (arguments.size() == 2)
    {
        addr = (MemAddr)strtoull( arguments[0].c_str(), &endptr, 0 );
        if (*endptr == '\0')
        {
            size = strtoul( arguments[1].c_str(), &endptr, 0 );
        }
    }

    if (arguments.size() != 2 || *endptr != '\0')
    {
        out << "Usage: read <mem> <address> <count>" << endl;
        return;
    }

    static const unsigned int BYTES_PER_LINE = 16;

    // Calculate aligned start and end addresses
    MemAddr start = addr / BYTES_PER_LINE * BYTES_PER_LINE;
    MemAddr end   = (addr + size + BYTES_PER_LINE - 1) / BYTES_PER_LINE * BYTES_PER_LINE;

    try
    {
        // Read the data
        vector<uint8_t> buf((size_t)size);
        Read(addr, &buf[0], size);

        // Print it
        for (MemAddr y = start; y < end; y += BYTES_PER_LINE)
        {
            // The address
            out << setw(8) << hex << setfill('0') << y << " | ";

            // The bytes
            for (MemAddr x = y; x < y + BYTES_PER_LINE; ++x)
            {
                if (x >= addr && x < addr + size)
                    out << setw(2) << (unsigned int)buf[(size_t)(x - addr)];
                else
                    out << "  ";

                // Print some space at half the grid
                if ((x - y) == BYTES_PER_LINE / 2 - 1) out << "  ";
                out << " ";
            }
            out << "| ";

            // The bytes, as characters
            for (MemAddr x = y; x < y + BYTES_PER_LINE; ++x)
            {
                char c = ' ';
                if (x >= addr && x < addr + size) {
                    c = buf[(size_t)(x - addr)];
                    c = (isprint(c) ? c : '.');
                }
                out << c;
            }
            out << endl;
        }
    }
    catch (exception &e)
    {
        out << "An exception occured while reading the memory:" << endl;
        out << e.what() << endl;
    }
}

void IMemoryAdmin::Cmd_Write(ostream& out, const vector<string>& arguments)
{
    MemAddr addr = 0;
    MemSize size = 0;
    char* endptr = NULL;

    if (arguments.size() >= 2)
    {
        addr = (MemAddr)strtoull( arguments[0].c_str(), &endptr, 0 );
        if (*endptr == '\0')
        {
            size = strtoul( arguments[1].c_str(), &endptr, 0 );
        }
    }

    if (arguments.size() <= 2 || *endptr != '\0')
    {
        out << "Usage: write <mem> <address> <size> <values...>" << endl;
        return;
    }

    std::vector<uint64_t> values;
    for (size_t i = 2; i < arguments.size(); ++i)
    {
        uint64_t value = (MemAddr)strtoull( arguments[i].c_str(), &endptr, 0 );
        if (*endptr != '\0')
        {
            out << "Invalid value format: " << arguments[i] << endl;
            return;
        }
        values.push_back(value);
    }

    try
    {
        vector<uint8_t> buf((size_t)size);

        for (size_t i = 0; i < values.size(); ++i) {
            auto val = values[i];
            out << "Writing " << val << " to address " << (addr + i * size) << endl;
            SerializeRegister(RT_INTEGER, val, &buf[0], size);
            Write(addr + i * size, &buf[0], 0, size);
        }
    }
    catch (exception &e)
    {
        out << "An exception occured while writing the memory:" << endl;
        out << e.what() << endl;
    }
}

}
