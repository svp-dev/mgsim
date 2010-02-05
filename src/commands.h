#ifndef COMMANDS_H
#define COMMANDS_H

#include "simreadline.h"
#include "MGSystem.h"
#include <vector>
#include <string>

void ExecuteCommand(Simulator::MGSystem& sys, 
                    const std::string& command, 
                    const std::vector<std::string>& args);

void HandleCommandLine(CommandLineReader& clr,
                       Simulator::MGSystem& sys,
                       bool &quit);


// Some public commands

void StepSystem(Simulator::MGSystem& system, Simulator::CycleNo cycles);


void PrintHelp(std::ostream&);
void PrintVersion(std::ostream&);
void PrintUsage(std::ostream& out, const char* cmd);
void PrintException(std::ostream& out, 
                    const std::exception& e);

void PrintComponents(std::ostream& out,
                     const Simulator::Object* root, 
                     const std::string& indent = "");
    
#endif
