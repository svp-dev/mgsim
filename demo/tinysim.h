// -*- c++ -*-
#ifndef TINYSIM_H
#define TINYSIM_H

#include "sim/config.h"
#include "sim/kernel.h"

class MGSim {
public:
    MGSim(const char *conf);

    Config* cfg;
    Simulator::Kernel* k;

    void DoSteps(Simulator::CycleNo nCycles);

private:
    MGSim(const MGSim&) = delete;
    MGSim& operator=(const MGSim&) = delete;
};

#endif
