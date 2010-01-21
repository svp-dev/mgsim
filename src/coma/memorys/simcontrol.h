#ifndef _SIM_CONTROL_H
#define _SIM_CONTROL_H

#include <ios>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdlib>

using namespace std;

namespace MemSim
{

class BusST_Master;

class SimObj
{
protected:
    // potential bus master
    BusST_Master* m_pBus;

public:
    SimObj() {}
    virtual ~SimObj() {}

    void SetBusMaster(BusST_Master* pbus)
    {
        m_pBus = pbus;
    }

    BusST_Master* GetBusMaster() const
    {
        return m_pBus;
    }
};

}

#endif
