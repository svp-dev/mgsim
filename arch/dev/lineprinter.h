#ifndef LINEPRINTER_H
#define LINEPRINTER_H

#include "MMIO.h"

namespace Simulator
{

class LinePrinter : public MMIOComponent
{
    std::ostream&  m_output;
    unsigned       m_floatprecision;

public:
    LinePrinter(const std::string& name, MMIOInterface& parent, std::ostream& output);

    size_t GetSize() const;
    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid) { return FAILED; }
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);
    
    ~LinePrinter() {}
};


}

#endif
