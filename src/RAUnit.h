#ifndef RAUNIT_H
#define RAUNIT_H

#include "kernel.h"

class Config;

namespace Simulator
{

class Processor;
class RegisterFile;

class RAUnit : public Object
{
    friend class RegisterFile;
public:
    typedef std::vector<std::pair<RegSize, LFID> > List;

    RAUnit(Processor& parent, const std::string& name, const RegisterFile& regFile, const Config& config);

    // LFID is for admin purposes only; not used for simulation
    bool Alloc(const RegSize size[NUM_REG_TYPES], LFID fid, RegIndex indices[NUM_REG_TYPES]);
    void Free(RegIndex indices[NUM_REG_TYPES]);

    // Interaction functions
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

private:
    List    m_lists[NUM_REG_TYPES];
    RegSize m_blockSizes[NUM_REG_TYPES];
};

};

#endif

