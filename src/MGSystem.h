#ifndef MGSYSTEM_H
#define MGSYSTEM_H

#include "Processor.h"
#include "FPU.h"
#include "kernel.h"
#include "Allocator.h"
#include "Memory.h"
#include "config.h"
#include "symtable.h"
#include "breakpoints.h"
#include "MMIO.h"
#include "lcd.h"

#include <vector>
#include <utility>
#include <string>

namespace Simulator {

    class MGSystem
    {
        Kernel                      m_kernel;
        Clock&                      m_clock;    ///< Master clock for the system
        Object                      m_root;     ///< Root object for the system
        std::vector<Processor*>     m_procs;
        std::vector<FPU*>           m_fpus;
        std::vector<LCD*>           m_lcds;
        SymbolTable                 m_symtable;
        BreakPoints                 m_breakpoints;
        IMemoryAdmin*               m_memory;
        std::string                 m_objdump_cmd;
        std::string                 m_program;
        enum {
            MEMTYPE_SERIAL = 1,
            MEMTYPE_PARALLEL = 2,
            MEMTYPE_BANKED = 3,
            MEMTYPE_RANDOMBANKED = 4,
            MEMTYPE_COMA_ZL = 5,
            MEMTYPE_COMA_ML = 6
        } m_memorytype; // for WriteConfiguration
        const Config&            m_config;
        std::vector<std::string> m_inputfiles;

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

        const Config& GetConfig() const { return m_config; }
        const std::string GetProgramName() const { return m_program; }
        const std::vector<std::string> GetInputFileNames() const { return m_inputfiles; }
        void FillConfWords(ConfWords&) const;

        // Get or set the debug flag
        int  GetDebugMode() const   { return m_kernel.GetDebugMode(); }
        void SetDebugMode(int mode) { m_kernel.SetDebugMode(mode); }
        void ToggleDebugMode(int mode) { m_kernel.ToggleDebugMode(mode); }

        uint64_t GetOp() const;
        uint64_t GetFlop() const;

        std::string GetSymbol(MemAddr addr) const;

        void Disassemble(MemAddr addr, size_t sz = 64) const;
        void PrintComponents(std::ostream& os) const;
        void PrintAllSymbols(std::ostream& os, const std::string& pat = "*") const;
        void PrintMemoryStatistics(std::ostream& os) const;
        void PrintState(const std::vector<std::string>& arguments) const;
        void PrintRegFileAsyncPortActivity(std::ostream& os) const;
        void PrintAllFamilyCompletions(std::ostream& os) const;
        void PrintFamilyCompletions(std::ostream& os) const;
        void PrintCoreStats(std::ostream& os) const;
        void PrintAllStatistics(std::ostream& os) const;

        const Kernel& GetKernel() const { return m_kernel; }
        Kernel& GetKernel()       { return m_kernel; }

        // Find a component in the system given its path
        // Returns NULL when the component is not found
        Object* GetComponent(const std::string& path);

        // Steps the entire system this many cycles
        void Step(CycleNo nCycles);
        void Abort() { GetKernel().Abort(); }
    
        MGSystem(const Config& config, Display& display, const std::string& program,
                 const std::string& symtable,
                 const std::vector<std::pair<RegAddr, RegValue> >& regs,
                 const std::vector<std::pair<RegAddr, std::string> >& loads,
                 bool quiet, bool doload);

        ~MGSystem();

        void DumpState(std::ostream& os);

    };

}

#endif
