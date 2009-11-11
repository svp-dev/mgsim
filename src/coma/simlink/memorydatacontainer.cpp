#include "memorydatacontainer.h"
#include "stdlib.h"
#include "assert.h"

MemoryDataContainer* MemoryDataContainer::s_pDataContainer = NULL;

MemoryDataContainer::MemoryDataContainer()
{
    // currently only one data container is allowed in the simulation system. 
    if (s_pDataContainer == NULL)
        s_pDataContainer = this;
    else
    {
        assert(false);
    }
}

