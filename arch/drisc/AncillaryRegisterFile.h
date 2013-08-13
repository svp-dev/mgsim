#ifndef ANCILLARYREGISTERFILE_H
#define ANCILLARYREGISTERFILE_H


#ifndef PROCESSOR_H
#error This file should be included in DRISC.h
#endif

/*
 * Access interface to ancillary registers. Ancillary registers
 * differ from regular (computational) registers in that
 * they are few and do not have state bits.
 * FIXME:
 * This should really be a register file with ports and arbitration etc.
 * Accessed R/W from the memory stage, W from delegation (remote configure)
 */

typedef size_t ARAddr;

class AncillaryRegisterFile : public drisc::MMIOComponent, public Inspect::Interface<Inspect::Read|Inspect::Info>
{
    const size_t                  m_numRegisters;
    std::vector<Integer>          m_registers;

public:
    AncillaryRegisterFile(const std::string& name, Object& parent, Config& config);

    size_t GetSize() const;

    Integer ReadRegister(ARAddr addr) const;
    void WriteRegister(ARAddr addr, Integer data);
    Result Read (MemAddr address, void* data, MemSize size, LFID fid, TID tid, const RegAddr& writeback);
    Result Write(MemAddr address, const void* data, MemSize size, LFID fid, TID tid);

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

};


#endif
