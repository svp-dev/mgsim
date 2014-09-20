#ifndef TINYSIM_H
#define TINYSIM_H

#include "sim/config.h"
#include "sim/kernel.h"

class MGSim {
private:
    ConfigMap overrides;
    std::vector<std::string> extras; // any additional strings that should be carried around by the Config class

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
