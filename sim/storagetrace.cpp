#include "storagetrace.h"
#include "storage.h"
#include <iostream>

using namespace std;

namespace Simulator
{

ostream& operator<<(ostream& os, const StorageTrace& st)
{
    for (size_t i = 0; i < st.m_storages.size(); ++i)
    {
        if (i != 0)
            os << ", ";
        os << st.m_storages[i]->GetName();
    }
    return os;
}

ostream& operator<<(ostream& os, const StorageTraceSet& sts)
{
    if (sts.m_storages.empty())
        os << "(no traces)" << endl;
    else
        for (auto& i : sts.m_storages)
            os << "- " << i << endl;

    return os;
}


}

