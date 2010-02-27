#include "breakpoints.h"
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <iomanip>

using namespace std;
using namespace Simulator;

void BreakPoints::CheckEnabled()
{
    bool someenabled;
    for (breakpoints_t::const_iterator i = m_breakpoints.begin();
         i != m_breakpoints.end(); ++i)
        if (i->second.enabled)
            someenabled = true;
    m_enabled = someenabled;
}

void BreakPoints::EnableBreakPoint(unsigned id)
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

void BreakPoints::ListBreakPoints(std::ostream& out) const
{
    if (m_breakpoints.empty())
        out << "no breakpoints defined." << endl;
    else 
    {
        out << "Id   | PC                 | Symbol               | Mode | Status    " << endl
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

void BreakPoints::Resume(void)
{
    m_activebreaks.clear();
}

void BreakPoints::ReportBreaks(std::ostream& out) const
{
    if (m_activebreaks.empty())
        return;

    out << "Breakpoints reached:" << endl << endl
        << "Id   | PC                 | Symbol               | Mode | Component      " << endl
        << "-----+--------------------+----------------------+------+----------------" << endl
        << setfill(' ') << left;

    for (active_breaks_t::const_iterator i = m_activebreaks.begin(); i != m_activebreaks.end(); ++i)
    {
        breakpoints_t::const_iterator b = m_breakpoints.find(i->addr);
        if (b == m_breakpoints.end())
            continue;

        std::string mode;
        if (i->type & READ)
            mode += 'R';
        if (i->type & WRITE)
            mode += 'W';
        if (i->type & EXEC)
            mode += 'X';

        out << setw(4) << dec << b->second.id << " | "
            << setw(18) << hex << showbase << i->addr << " | " 
            << setw(20) << m_kernel.GetSymbolTable()[i->addr] << " | "
            << setw(4) << mode << " | "
            << i->obj->GetFQN()
            << endl;
    }
}

void BreakPoints::ClearAllBreakPoints(void)
{
    m_breakpoints.clear();
    m_enabled = false;
}

void BreakPoints::DisableBreakPoint(unsigned id)
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

void BreakPoints::DeleteBreakPoint(unsigned id)
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

void BreakPoints::AddBreakPoint(MemAddr addr, int type)
{
    BreakPointInfo info;
    info.enabled = true;
    info.id = m_counter++;
    info.type = type;
    m_breakpoints[addr] = info;
    m_enabled = true;
}

void BreakPoints::AddBreakPoint(const std::string& sym, int offset, int type)
{
    errno = 0;
    MemAddr addr = strtoul(sym.c_str(), 0, 0);
    if (errno == EINVAL)
    {
        bool check = m_kernel.GetSymbolTable().LookUp(sym, addr);
        if (!check)
        {
            cerr << "invalid address: " << sym << endl;
            return;
        }
    }
    AddBreakPoint(addr + offset, type);
}


void BreakPoints::CheckMore(int type, MemAddr addr, Object& obj)
{
    breakpoints_t::const_iterator i = m_breakpoints.find(addr);
    if (i != m_breakpoints.end() && i->second.enabled && (i->second.type & type) != 0) 
    {
        ActiveBreak ab(addr, obj, i->second.type & type);
        m_activebreaks.insert(ab);
        m_kernel.Abort();
    }
}
