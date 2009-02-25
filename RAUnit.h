#ifndef RAUNIT_H
#define RAUNIT_H

#include "kernel.h"

namespace Simulator
{

class Processor;
class RegisterFile;

class RAUnit : public Object
{
public:
	struct Config
	{
		RegSize blockSizes[NUM_REG_TYPES];
	};

    typedef std::vector<std::pair<RegSize, LFID> > List;

    RAUnit(Processor& parent, const std::string& name, const RegisterFile& regFile, const Config& config);

    // LFID is for admin purposes only; not used for simulation
    bool Alloc(const RegSize size[NUM_REG_TYPES], LFID fid, RegIndex indices[NUM_REG_TYPES]);
    bool Free(RegIndex indices[NUM_REG_TYPES]);

    // Admin functions
    const List& GetList(RegType type) const { return m_lists[type]; }
    RegSize GetBlockSize(RegType type) const { return m_blockSizes[type]; }

private:
    List    m_lists[NUM_REG_TYPES];
    RegSize m_blockSizes[NUM_REG_TYPES];
};

};

#endif

