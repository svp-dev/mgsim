#include "storagetrace.h"
#include "storage.h"
#include <iostream>

namespace Simulator
{

std::ostream& operator<<(std::ostream& os, const StorageTrace& st)
{
    for (size_t i = 0; i < st.m_storages.size(); ++i)
    {
        if (i != 0)
            os << ", ";
        os << st.m_storages[i]->GetFQN();
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const StorageTraceSet& sts)
{
    if (sts.m_storages.empty())
    {
        os << "(no traces)" << std::endl;
    }
    else
    {
        for (std::set<StorageTrace>::const_iterator i = sts.m_storages.begin(); i != sts.m_storages.end(); ++i)
        {
            os << "- " << *i << std::endl;
        }
    }
    return os;
}


}

