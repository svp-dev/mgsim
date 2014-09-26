#include <cstdio>
#include "sim/kernel.h"

namespace Simulator
{

    Object::Object(const std::string& name, Kernel&
#ifndef STATIC_KERNEL
                   k
#endif
        )
        : m_parent(NULL),
          m_name(name),
#ifndef STATIC_KERNEL
          m_kernel(k),
#endif
          m_children()
    {
    }

    Object::Object(const std::string& name, Object& parent)
        : m_parent(&parent),
          m_name(parent.GetName().empty() ? name : (parent.GetName() + '.' + name)),
#ifndef STATIC_KERNEL
          m_kernel(*parent.GetKernel()),
#endif
          m_children()
    {
        // Add ourself to the parent's children array
        parent.m_children.push_back(this);
    }

    Object::~Object()
    {
        if (m_parent != NULL)
        {
            // Remove ourself from the parent's children array
            for (auto p = m_parent->m_children.begin(); p != m_parent->m_children.end(); ++p)
            {
                if (*p == this)
                {
                    m_parent->m_children.erase(p);
                    break;
                }
            }
        }
    }

    void Object::OutputWrite_(const char* msg, ...) const
    {
        va_list args;

        fprintf(stderr, "[%08lld:%s]\t\to ", (unsigned long long)GetKernel()->GetCycleNo(), GetName().c_str());
        va_start(args, msg);
        vfprintf(stderr, msg, args);
        va_end(args);
        fputc('\n', stderr);
    }

    void Object::DeadlockWrite_(const char* msg, ...) const
    {
        va_list args;

        fprintf(stderr, "[%08lld:%s]\t(%s)\td ", (unsigned long long)GetKernel()->GetCycleNo(), GetName().c_str(),
                GetKernel()->GetActiveProcess()->GetName().c_str());
        va_start(args, msg);
        vfprintf(stderr, msg, args);
        va_end(args);
        fputc('\n', stderr);
    }

    void Object::DebugSimWrite_(const char* msg, ...) const
    {
        va_list args;

        fprintf(stderr, "[%08lld:%s]\t", (unsigned long long)GetKernel()->GetCycleNo(), GetName().c_str());
        const Process *p = GetKernel()->GetActiveProcess();
        if (p)
            fprintf(stderr, "(%s)", p->GetName().c_str());
        fputc('\t', stderr);
        va_start(args, msg);
        vfprintf(stderr, msg, args);
        va_end(args);
        fputc('\n', stderr);
    }


}
