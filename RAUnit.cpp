#include <cassert>
#include "RAUnit.h"
#include "RegisterFile.h"
#include "Processor.h"

using namespace Simulator;
using namespace std;

RAUnit::RAUnit(Processor& parent, const std::string& name, const RegisterFile& regFile, const Config& config)
    : Object(&parent, &parent.getKernel(), name)
{
    for (RegType i = 0; i < NUM_REG_TYPES; i++)
    {
        RegSize size = regFile.getSize(i);

        m_blockSizes[i] = config.blockSizes[i];
        if (m_blockSizes[i] == 0 || (m_blockSizes[i] & (m_blockSizes[i] - 1)) != 0)
        {
            throw InvalidArgumentException("Allocation block size is not a power of two");
        }

        if (size % m_blockSizes[i] != 0)
        {
            throw InvalidArgumentException("Register counts aren't a multiple of the allocation block size");
        }
    
        m_lists[i].resize(size / m_blockSizes[i], pair<RegSize,LFID>(0, INVALID_LFID));
    }
}

bool RAUnit::alloc(const RegSize sizes[NUM_REG_TYPES], LFID fid, RegIndex indices[NUM_REG_TYPES])
{
	for (RegType i = 0; i < NUM_REG_TYPES; i++)
	{
		indices[i] = INVALID_REG_INDEX;
		if (sizes[i] != 0)
		{
			// Get number of blocks (ceil)
			RegSize size = (sizes[i] + m_blockSizes[i] - 1) / m_blockSizes[i];

			List& list = m_lists[i];
			for (RegIndex pos = 0; pos < list.size() && indices[i] == INVALID_REG_INDEX;)
			{
				if (list[pos].first != 0)
				{
					pos += list[pos].first;
				}
				else
				{
					// Free area, check size
					for (RegIndex start = pos; pos < list.size() && list[pos].first == 0; pos++)
					{
						if (pos - start + 1 == size)
						{
							indices[i] = start * m_blockSizes[i];
							break;
						}
					}
				}
			}

			if (indices[i] == INVALID_REG_INDEX)
			{
				// Couldn't get a block for this type
				return false;
			}
		}
	}

	COMMIT
	( 
		// We've found blocks for all types, commit them
		for (RegType i = 0; i < NUM_REG_TYPES; i++)
		{
			if (sizes[i] != 0)
			{
				RegSize size = (sizes[i] + m_blockSizes[i] - 1) / m_blockSizes[i];
				m_lists[i][indices[i] / m_blockSizes[i]].first  = size;
				m_lists[i][indices[i] / m_blockSizes[i]].second = fid;
			}
		}
	)

	return true;
}

bool RAUnit::free(RegIndex indices[NUM_REG_TYPES])
{
	for (RegType i = 0; i < NUM_REG_TYPES; i++)
	{
		if (indices[i] != INVALID_REG_INDEX)
		{
			// Floor to block size
			RegIndex index = indices[i] / m_blockSizes[i];

			List& list = m_lists[i];
			assert(list[index].first != 0);

			COMMIT( list[index].first = 0; )
		}
	}
    return true;
}
