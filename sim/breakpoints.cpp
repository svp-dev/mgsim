#include "breakpoints.h"
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <iomanip>

using namespace std;

namespace Simulator
{

void BreakPointManager::CheckEnabled()
{
    bool someenabled = false;
    for (breakpoints_t::const_iterator i = m_breakpoints.begin();
         i != m_breakpoints.end(); ++i)
        if (i->second.enabled)
            someenabled = true;
    m_enabled = someenabled;
}

void BreakPointManager::EnableBreakPoint(unsigned id)
{
    bool found = false;
    for (breakpoints_t::iterator i = m_breakpoints.begin();
         i != m_breakpoints.end(); ++i)
        if (i->second.id == id) 
        {
            found = true;
            i->second.enabled = true;
            break;
        }
    if (found)
        m_enabled = true;
    else
    {
        cerr << "invalid breakpoint" << endl;
        return; 
    }
}

void BreakPointManager::ListBreakPoints(std::ostream& out) const
{
    if (m_breakpoints.empty())
        out << "no breakpoints defined." << endl;
    else 
    {
        out << "Id   | Address            | Symbol               | Mode | Status    " << endl
            << "-----+--------------------+----------------------+------+-----------" << endl
            << setfill(' ') << left;
        for (breakpoints_t::const_iterator i = m_breakpoints.begin();
             i != m_breakpoints.end(); ++i)
        {
            std::string mode;
            if (i->second.type & READ)
                mode += 'R';
            if (i->second.type & WRITE)
                mode += 'W';
            if (i->second.type & EXEC)
                mode += 'X';
            if (i->second.type & TRACEONLY)
                mode += 'T';
            
            out << setw(4) << dec << i->second.id << " | "
                << setw(18) << hex << showbase << i->first << " | " 
                << setw(20) << m_kernel.GetSymbolTable()[i->first] << " | "
                << setw(4) << mode << " | "
                << setw(9) << (i->second.enabled ? "enabled" : "disabled")
                << endl;
        }
    }
    out << endl;
    out << "Breakpoint checking is " << (m_enabled ? "enabled" : "disabled") << '.' << endl;
}

void BreakPointManager::Resume(void)
{
    m_activebreaks.clear();
}

std::string BreakPointManager::GetModeName(int type)
{
    std::string mode;
    if (type & READ)
        mode += 'R';
    if (type & WRITE)
        mode += 'W';
    if (type & EXEC)
        mode += 'X';
    if (type & TRACEONLY)
        mode += 'T';
    return mode;
}

void BreakPointManager::ReportBreaks(std::ostream& out) const
{
    if (m_activebreaks.empty())
        return;

    out << "Breakpoints reached:" << endl << endl
        << "Id   | Address            | Symbol               | Mode | Component      " << endl
        << "-----+--------------------+----------------------+------+----------------" << endl
        << setfill(' ') << left;

    for (active_breaks_t::const_iterator i = m_activebreaks.begin(); i != m_activebreaks.end(); ++i)
    {
        breakpoints_t::const_iterator b = m_breakpoints.find(i->addr);
        if (b == m_breakpoints.end())
            continue;

        out << setw(4) << dec << b->second.id << " | "
            << setw(18) << hex << showbase << i->addr << " | " 
            << setw(20) << m_kernel.GetSymbolTable()[i->addr] << " | "
            << setw(4) << GetModeName(i->type) << " | "
            << i->obj->GetFQN()
            << endl;
    }
}

void BreakPointManager::ClearAllBreakPoints(void)
{
    m_breakpoints.clear();
    m_enabled = false;
}

void BreakPointManager::DisableBreakPoint(unsigned id)
{
    bool found = false;

    for (breakpoints_t::iterator i = m_breakpoints.begin();
         i != m_breakpoints.end(); ++i)
        if (i->second.id == id) 
        {
            found = true;
            i->second.enabled = false;
            break;
        }

    if (!found)
    {
        cerr << "invalid breakpoint" << endl;
        return; 
    }

    CheckEnabled();
}

void BreakPointManager::DeleteBreakPoint(unsigned id)
{
    bool found = false;

    for (breakpoints_t::iterator i = m_breakpoints.begin();
         i != m_breakpoints.end(); ++i)
        if (i->second.id == id) 
        {
            found = true;
            m_breakpoints.erase(i);
            break;
        }

    if (!found)
    {
        cerr << "invalid breakpoint" << endl;
        return; 
    }

    CheckEnabled();
}

void BreakPointManager::AddBreakPoint(MemAddr addr, int type)
{
    BreakPointInfo info;
    info.enabled = true;
    info.id = m_counter++;
    info.type = type;

    breakpoints_t::const_iterator i = m_breakpoints.find(addr);
    if (i != m_breakpoints.end())
        info.type |= i->second.type;

    m_breakpoints[addr] = info;
    m_enabled = true;
}

void BreakPointManager::AddBreakPoint(const std::string& sym, int offset, int type)
{
    errno = 0;
    MemAddr addr = strtoull(sym.c_str(), 0, 0);
    if (errno == EINVAL)
    {
        bool check = m_kernel.GetSymbolTable().LookUp(sym, addr, true);
        if (!check)
        {
            cerr << "invalid address: " << sym << endl;
            return;
        }
    }
    AddBreakPoint(addr + offset, type);
}


void BreakPointManager::CheckMore(int type, MemAddr addr, Object& obj)
{
    breakpoints_t::const_iterator i = m_breakpoints.find(addr);
    if (i != m_breakpoints.end() && i->second.enabled && (i->second.type & type) != 0) 
    {
        if (i->second.type & TRACEONLY)
        {
            if (m_kernel.GetCyclePhase() == PHASE_COMMIT)
            {
                obj.DebugSimWrite_("Trace point %d reached: 0x%.*llx (%s, %s)", 
                                   i->second.id, (int)sizeof(addr)*2, (unsigned long long)addr, 
                                   m_kernel.GetSymbolTable()[addr].c_str(), 
                                   GetModeName(i->second.type & type).c_str());
            }
        }
        else
        {
            ActiveBreak ab(addr, obj, i->second.type & type);
            m_activebreaks.insert(ab);
            m_kernel.Stop();
        }
    }
}

}

