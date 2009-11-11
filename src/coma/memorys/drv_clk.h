#ifndef _DRV_CLK_H
#define _DRV_CLK_H

#include "predef.h"

namespace MemSim{
//{ memory simulator namespace
//////////////////////////////


class DRV_clk : public sc_module
{
public:
    sc_in<bool> port_clk;

private:
    bool m_bStep;

public:
    SC_HAS_PROCESS(DRV_clk);
    DRV_clk(sc_module_name nm, bool step = false) : sc_module(nm), m_bStep(step)
    {
        if (step)
        {
            SC_THREAD(StepBehavior);
            sensitive << port_clk.pos();
            

            SC_THREAD(StepBehavior);
            sensitive << port_clk.neg();
        }
    }

    void StepBehavior()
    {
        wait(0, SC_NS);
        sc_stop();
    }
};

//////////////////////////////
//} memory simulator namespace
}

#endif
