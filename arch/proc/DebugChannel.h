#ifndef DEBUGCHANNEL_H
#define DEBUGCHANNEL_H


#ifndef PROCESSOR_H
#error This file should be included in Processor.h
#endif

class DebugChannel : public MMIOComponent
{
    std::ostream&  m_output;
    unsigned       m_floatprecision;

public:
    DebugChannel(const std::string& name, Object& parent, std::ostream& output);

    size_t GetSize() const;

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

};



#endif
