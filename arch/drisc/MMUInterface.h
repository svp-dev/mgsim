#ifndef MMUINTERFACE_H
#define MMUINTERFACE_H

#include "IOMatchUnit.h"

namespace Simulator
{
namespace drisc
{

class MMUInterface : public MMIOComponent
{
public:
    MMUInterface(const std::string& name, Object& parent);

    size_t GetSize() const;

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

};

}
}

#endif
