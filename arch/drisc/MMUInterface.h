#ifndef MMUINTERFACE_H
#define MMUINTERFACE_H


#ifndef PROCESSOR_H
#error This file should be included in DRISC.h
#endif

class MMUInterface : public drisc::MMIOComponent
{
public:
    MMUInterface(const std::string& name, Object& parent);

    size_t GetSize() const;

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

};



#endif
