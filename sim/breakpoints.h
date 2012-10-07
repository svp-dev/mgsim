#ifndef BREAKPOINTS_H
#define BREAKPOINTS_H

#include <sim/kernel.h>
#include <arch/symtable.h>
#include <sim/except.h>

#include <map>
#include <string>
#include <iostream>

namespace Simulator
{

class BreakPointManager
{
public:
    enum BreakPointType { 
        FETCH = 1,
        EXEC = 2, 
        MEMREAD = 4, 
        MEMWRITE = 8, 
        TRACEONLY = 16,
    };

private:
    struct BreakPointInfo {
        bool              enabled;
        unsigned          id;
        int               type;
    };

    typedef std::map<Simulator::MemAddr, BreakPointInfo> breakpoints_t;

    struct ActiveBreak {
        Simulator::MemAddr addr;
        Simulator::Object  *obj;
        int                type;

        ActiveBreak(Simulator::MemAddr addr_, Simulator::Object& obj_, int type_)
        : addr(addr_), obj(&obj_), type(type_) {}

        // For std::set
        bool operator<(const ActiveBreak& other) const
        { 
            return (addr < other.addr) ||
                (addr == other.addr && (obj < other.obj)) ||
                (obj == other.obj && type < other.type); 
        }
    };

    typedef std::set<ActiveBreak> active_breaks_t;


    unsigned           m_counter;
    bool               m_enabled;
    breakpoints_t      m_breakpoints;
    active_breaks_t    m_activebreaks;
    Kernel&            m_kernel;
    SymbolTable*       m_symtable;

    void CheckMore(int type, Simulator::MemAddr addr, Simulator::Object& obj);
    void CheckEnabled(void);

    static std::string GetModeName(int);
public:
    BreakPointManager(Simulator::Kernel& kernel, SymbolTable* symtable = 0) 
      : m_counter(0), m_enabled(false), m_kernel(kernel), m_symtable(symtable) {}

    void EnableCheck(void) { m_enabled = true; }
    void DisableCheck(void) { m_enabled = false; }

    void EnableBreakPoint(unsigned id);
    void DisableBreakPoint(unsigned id);
    void DeleteBreakPoint(unsigned id);

    void AddBreakPoint(Simulator::MemAddr addr, int type = EXEC);
    void AddBreakPoint(const std::string& sym, int offset, int type = EXEC);

    void ClearAllBreakPoints(void);
    void ListBreakPoints(std::ostream& out) const;

    // Call Resume() once after some breakpoints have been
    // encountered and before execution should resume.
    void Resume(void);

    void ReportBreaks(std::ostream& out) const;

    bool NewBreaksDetected(void) const { return !m_activebreaks.empty(); }

    void Check(int type, Simulator::MemAddr addr, Simulator::Object& obj) 
    {
        if (m_enabled)
            CheckMore(type, addr, obj);
    }

    void SetSymbolTable(SymbolTable &symtable) { m_symtable = &symtable; }
    SymbolTable& GetSymbolTable() const { return *m_symtable; }
};

}

#endif
