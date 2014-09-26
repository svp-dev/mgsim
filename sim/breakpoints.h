// -*- c++ -*-
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
        unsigned          id;
        int               type;
        bool              enabled;
    };

    typedef std::map<MemAddr, BreakPointInfo> breakpoints_t;

    struct ActiveBreak {
        MemAddr addr;
        Object  *obj;
        int     type;

        ActiveBreak(MemAddr addr_, Object& obj_, int type_)
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


    breakpoints_t      m_breakpoints;
    active_breaks_t    m_activebreaks;
#ifndef STATIC_KERNEL
    Kernel*            m_kernel;
#endif
    SymbolTable*       m_symtable;
    unsigned           m_counter;
    bool               m_enabled;

    void CheckMore(int type, MemAddr addr, Object& obj);
    void CheckEnabled(void);

    static std::string GetModeName(int);
#ifdef STATIC_KERNEL
    static Kernel* GetKernel() { return &Kernel::GetGlobalKernel(); }
#else
    Kernel* GetKernel() { return m_kernel; }
#endif

public:
    BreakPointManager(SymbolTable* symtable = 0)
        : m_breakpoints(), m_activebreaks(),
#ifndef STATIC_KERNEL
        m_kernel(0), 
#endif
	m_symtable(symtable),
        m_counter(0), m_enabled(false) {}

    BreakPointManager(const BreakPointManager& other)
        : m_breakpoints(other.m_breakpoints), m_activebreaks(other.m_activebreaks),
#ifndef STATIC_KERNEL
        m_kernel(other.m_kernel), 
#endif
	m_symtable(other.m_symtable),
        m_counter(other.m_counter), m_enabled(other.m_enabled) {}
    BreakPointManager& operator=(const BreakPointManager& other) = delete;

#ifdef STATIC_KERNEL
    void AttachKernel(Kernel&) {}
#else
    void AttachKernel(Kernel& k) { m_kernel = &k; }
#endif

    void EnableCheck(void) { m_enabled = true; }
    void DisableCheck(void) { m_enabled = false; }

    void EnableBreakPoint(unsigned id);
    void DisableBreakPoint(unsigned id);
    void DeleteBreakPoint(unsigned id);

    void AddBreakPoint(MemAddr addr, int type = EXEC);
    void AddBreakPoint(const std::string& sym, int offset, int type = EXEC);

    void ClearAllBreakPoints(void);
    void ListBreakPoints(std::ostream& out) const;

    // Call Resume() once after some breakpoints have been
    // encountered and before execution should resume.
    void Resume(void);

    void ReportBreaks(std::ostream& out) const;

    bool NewBreaksDetected(void) const { return !m_activebreaks.empty(); }

    void Check(int type, MemAddr addr, Object& obj)
    {
        if (m_enabled)
            CheckMore(type, addr, obj);
    }

    void SetSymbolTable(SymbolTable &symtable) { m_symtable = &symtable; }
    SymbolTable& GetSymbolTable() const { return *m_symtable; }
};

}

#endif
