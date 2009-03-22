#ifndef COMMANDS_H
#define COMMANDS_H

#include "kernel.h"

//
// The commands array
// This holds a list of all (normal) commands
//
struct COMMAND
{
    const char*      name;
    bool (*execute)(Simulator::Object* obj, const std::vector<std::string>&);
};

extern const COMMAND Commands[];

#endif

