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
    for (auto& i : m_breakpoints)
        if (i.second.enabled)
        {
            someenabled = true;
            break;
        }
    m_enabled = someenabled;
}

void BreakPointManager::EnableBreakPoint(unsigned id)
{
    bool found = false;
    for (auto& i : m_breakpoints)
        if (i.second.id == id)
        {
            found = true;
            i.second.enabled = true;
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
        out << "Id   | Address            | Symbol               | Mode  | Status    " << endl
            << "-----+--------------------+----------------------+-------+-----------" << endl
            << setfill(' ') << left;
        for (auto& i : m_breakpoints)
        {
            std::string mode;
            if (i.second.type & FETCH)
                mode += 'F';
            if (i.second.type & EXEC)
                mode += 'X';
            if (i.second.type & MEMREAD)
                mode += 'R';
            if (i.second.type & MEMWRITE)
                mode += 'W';
            if (i.second.type & TRACEONLY)
                mode += 'T';

            out << setw(4) << dec << i.second.id << " | "
                << setw(18) << hex << showbase << i.first << " | "
                << setw(20) << GetSymbolTable()[i.first] << " | "
                << setw(5) << mode << " | "
                << setw(9) << (i.second.enabled ? "enabled" : "disabled")
                << endl;
        }
    }
    out << endl
        << "Breakpoint checking is " << (m_enabled ? "enabled" : "disabled") << '.' << endl;
}

void BreakPointManager::Resume(void)
{
    m_activebreaks.clear();
}

std::string BreakPointManager::GetModeName(int type)
{
    std::string mode;
    if (type & FETCH)
        mode += 'F';
    if (type & EXEC)
        mode += 'X';
    if (type & MEMREAD)
        mode += 'R';
    if (type & MEMWRITE)
        mode += 'W';
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

    for (auto& i : m_activebreaks)
    {
        auto b = m_breakpoints.find(i.addr);
        if (b == m_breakpoints.end())
            continue;

        out << setw(4) << dec << b->second.id << " | "
            << setw(18) << hex << showbase << i.addr << " | "
            << setw(20) << GetSymbolTable()[i.addr] << " | "
            << setw(4) << GetModeName(i.type) << " | "
            << i.obj->GetFQN()
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

    for (auto& i : m_breakpoints)
        if (i.second.id == id)
        {
            found = true;
            i.second.enabled = false;
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

    for (auto i = m_breakpoints.begin(); i != m_breakpoints.end(); ++i)
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

    auto i = m_breakpoints.find(addr);
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
        bool check = GetSymbolTable().LookUp(sym, addr, true);
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
    auto i = m_breakpoints.find(addr);
    if (i != m_breakpoints.end() && i->second.enabled && (i->second.type & type) != 0)
    {
        if (i->second.type & TRACEONLY)
        {
            if (GetKernel()->GetCyclePhase() == PHASE_COMMIT)
            {
                obj.DebugSimWrite_("Trace point %d reached: 0x%.*llx (%s, %s)",
                                   i->second.id, (int)sizeof(addr)*2, (unsigned long long)addr,
                                   GetSymbolTable()[addr].c_str(),
                                   GetModeName(i->second.type & type).c_str());
            }
        }
        else
        {
            ActiveBreak ab(addr, obj, i->second.type & type);
            m_activebreaks.insert(ab);
            GetKernel()->Stop();
        }
    }
}

}

