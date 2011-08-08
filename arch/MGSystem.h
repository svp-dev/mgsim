#ifndef MGSYSTEM_H
#define MGSYSTEM_H

#include "arch/proc/Processor.h"
#include "arch/FPU.h"
#include "arch/symtable.h"
#include "arch/IOBus.h"
#include "sim/breakpoints.h"
#include "sim/config.h"

#include <vector>
#include <utility>
#include <string>
#include <map>

namespace Simulator {

    class ActiveROM;
    class Selector;

    class MGSystem
    {
        Kernel                      m_kernel;
        Clock&                      m_clock;    ///< Master clock for the system
        Object                      m_root;     ///< Root object for the system
        std::vector<Processor*>     m_procs;
        std::vector<FPU*>           m_fpus;
        std::vector<IIOBus*>        m_iobuses;
        std::vector<Object*>        m_devices;
        std::map<size_t, size_t>    m_procbusmapping;

        SymbolTable                 m_symtable;
        BreakPoints                 m_breakpoints;
        IMemoryAdmin*               m_memory;
        std::string                 m_objdump_cmd;
        Config&            m_config;
        ActiveROM*         m_bootrom;
        Selector*          m_selector;

        // Writes the current configuration into memory and returns its address
        MemAddr WriteConfiguration();

    public:
        struct ConfWords
        {
            std::vector<uint32_t> data;
            
            ConfWords& operator<<(uint32_t val) 
            {
                uint32_t repr;
                SerializeRegister(RT_INTEGER, val, &repr, sizeof(repr));
                data.push_back(repr);
                return *this;
            }
        };

        Config& GetConfig() const { return m_config; }

        // Get or set the debug flag
        int  GetDebugMode() const   { return m_kernel.GetDebugMode(); }
        void SetDebugMode(int mode) { m_kernel.SetDebugMode(mode); }
        void ToggleDebugMode(int mode) { m_kernel.ToggleDebugMode(mode); }

        uint64_t GetOp() const;
        uint64_t GetFlop() const;

        void Disassemble(MemAddr addr, size_t sz = 64) const;

        typedef std::map<std::string, Object*> object_map_t;
        object_map_t GetComponents(const std::string& pat = "*");

        void PrintComponents(std::ostream& os, const std::string& pat = "*", size_t levels = 0) const;
        void PrintProcesses(std::ostream& os, const std::string& pat = "*") const;
        
        void DumpArea(std::ostream& os, unsigned int tech) const;

        void PrintMemoryStatistics(std::ostream& os) const;
        void PrintState(const std::vector<std::string>& arguments) const;
        void PrintRegFileAsyncPortActivity(std::ostream& os) const;
        void PrintAllFamilyCompletions(std::ostream& os) const;
        void PrintFamilyCompletions(std::ostream& os) const;
        void PrintCoreStats(std::ostream& os) const;
        void PrintAllStatistics(std::ostream& os) const;

        const Kernel& GetKernel() const { return m_kernel; }
        Kernel& GetKernel()       { return m_kernel; }

        const SymbolTable& GetSymTable() const { return m_symtable; }

        // Steps the entire system this many cycles
        void Step(CycleNo nCycles);
        void Abort() { GetKernel().Abort(); }
    
        MGSystem(Config& config,
                 const std::string& symtable,
                 const std::vector<std::pair<RegAddr, RegValue> >& regs,
                 const std::vector<std::pair<RegAddr, std::string> >& loads,
                 const std::vector<std::string>& extradevs,
                 bool quiet, bool doload);

        ~MGSystem();

        void DumpState(std::ostream& os);

    };

}

#endif
