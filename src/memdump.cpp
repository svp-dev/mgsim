#include "memdump.h"
#include <fstream>
#include <iostream>

using namespace std;

void DumpMemory(Simulator::IMemoryAdmin* memory, const std::string& path, Simulator::MemAddr address, Simulator::MemSize size)
{
    ofstream output(path.c_str(), ios::binary);
    if (!output.is_open() || !output.good())
    {
        throw runtime_error("Cannot open memory dump file: " + path);
    }

    char* data = new char[size_t(size)];
    memory->Read(address, data, size);
    output.write(data, streamsize(size));
    delete[] data;
    output.close();
}
