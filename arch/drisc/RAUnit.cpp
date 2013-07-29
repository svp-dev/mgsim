#include "DRISC.h"
#include <sim/config.h>
#include <sim/log2.h>

#include <cassert>
#include <iomanip>
using namespace std;

namespace Simulator
{

DRISC::RAUnit::RAUnit(const std::string& name, DRISC& parent, Clock& clock, const RegisterFile& regFile, Config& config)
    : Object(name, parent, clock)
{
    static struct RegTypeInfo {
        const char* blocksize_name;
        RegSize     context_size;
    } RegTypeInfos[NUM_REG_TYPES] = {
        {"IntRegistersBlockSize", 32},
        {"FltRegistersBlockSize", 32}
    };

    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        TypeInfo&          type = m_types[i];
        const RegTypeInfo& info = RegTypeInfos[i];

        // Contexts must be a power of two
        assert(IsPowerOfTwo(info.context_size));

        type.blockSize = config.getValue<size_t>(*this, info.blocksize_name);
        if (type.blockSize == 0 || !IsPowerOfTwo(type.blockSize))
        {
            throw exceptf<InvalidArgumentException>(*this, "%s is not a power of two", info.blocksize_name);
        }

        // A block must be able to fit at least a context
        if (type.blockSize < info.context_size)
        {
            throw exceptf<InvalidArgumentException>(*this, "%s is smaller than a context", info.blocksize_name);
        }

        const RegSize size = regFile.GetSize((RegType)i);
        if (size % type.blockSize != 0)
        {
            throw exceptf<InvalidArgumentException>(*this, "%s does not divide the register file size", info.blocksize_name);
        }

        const BlockSize free_blocks = size / type.blockSize;
        if (free_blocks < 2)
        {
            throw exceptf<InvalidArgumentException>(*this, "%s: there must be at least two allocation blocks of registers", info.blocksize_name);
        }

        type.free[CONTEXT_NORMAL]    = free_blocks - 1;
        type.free[CONTEXT_RESERVED]  = 0;
        type.free[CONTEXT_EXCLUSIVE] = 1;

        type.list.resize(free_blocks, List::value_type(0, INVALID_LFID));
    }
}

DRISC::RAUnit::BlockSize DRISC::RAUnit::GetNumFreeContexts(ContextType type) const
{
    // Return the smallest number of free contexts.
    BlockSize free = m_types[0].free[type];
    for (size_t i = 1; i < NUM_REG_TYPES; ++i)
    {
        free = std::min(free, m_types[i].free[type]);
    }
    return free;
}

void DRISC::RAUnit::ReserveContext()
{
    // Move a normal context to reserved
    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        assert(m_types[i].free[CONTEXT_NORMAL] > 0);
        COMMIT{
            m_types[i].free[CONTEXT_NORMAL]--;
            m_types[i].free[CONTEXT_RESERVED]++;
        }
    }
}

void DRISC::RAUnit::UnreserveContext()
{
    // Move a reserved context to normal
    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        assert(m_types[i].free[CONTEXT_RESERVED] > 0);
        COMMIT{
            m_types[i].free[CONTEXT_RESERVED]--;
            m_types[i].free[CONTEXT_NORMAL]++;
        }
    }
}

bool DRISC::RAUnit::Alloc(const RegSize sizes[NUM_REG_TYPES], LFID fid, ContextType context, RegIndex indices[NUM_REG_TYPES])
{
    BlockSize blocksizes[NUM_REG_TYPES];

    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        TypeInfo& type = m_types[i];

        indices[i] = INVALID_REG_INDEX;
        if (sizes[i] != 0)
        {
            // Get number of blocks (round up to nearest block size)
            BlockSize size = blocksizes[i] = (sizes[i] + type.blockSize - 1) / type.blockSize;

            // Check if have enough blocks free to even start looking
            BlockSize free = type.free[CONTEXT_NORMAL];
            if (context != CONTEXT_NORMAL)
            {
                // We have an extra block to include in our search
                free++;
            }
            assert(free <= type.list.size());

            if (free >= size)
            {
                // We have enough free space, find a contiguous free area of specified size
                for (RegIndex pos = 0; pos < type.list.size() && indices[i] == INVALID_REG_INDEX;)
                {
                    if (type.list[pos].first != 0)
                    {
                        // Used area, skip past it
                        pos += type.list[pos].first;
                    }
                    else
                    {
                        // Free area, check size
                        for (RegIndex start = pos; pos < type.list.size() && type.list[pos].first == 0; ++pos)
                        {
                            if (pos - start + 1 == size)
                            {
                                // We found a free area, store it's base register index
                                indices[i] = start * type.blockSize;
                                break;
                            }
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
    {
        // We've found blocks for all types, commit them
        for (size_t i = 0; i < NUM_REG_TYPES; ++i)
        {
            TypeInfo& type = m_types[i];
            if (sizes[i] != 0)
            {
                BlockSize size = blocksizes[i];

                type.list[indices[i] / type.blockSize].first  = size;
                type.list[indices[i] / type.blockSize].second = fid;

                if (context != CONTEXT_NORMAL)
                {
                    // We used one special context
                    assert(type.free[context] > 0);
                    type.free[context]--;

                    // The rest come from the normal contexts' pool
                    size--;
                }
                assert(type.free[CONTEXT_NORMAL] >= size);
                type.free[CONTEXT_NORMAL] -= size;
            }
            else if (context == CONTEXT_RESERVED)
            {
                // We've reserved a context, but aren't using it.
                // Release it back to the normal pool.
                type.free[CONTEXT_NORMAL]++;
            }
        }
    }

    return true;
}

void DRISC::RAUnit::Free(RegIndex indices[NUM_REG_TYPES], ContextType context)
{
    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        TypeInfo& type = m_types[i];
        if (indices[i] != INVALID_REG_INDEX)
        {
            // Get block index and allocated size
            BlockIndex index = indices[i] / type.blockSize;
            BlockSize  size  = type.list[index].first;

            assert(size != 0);

            COMMIT{
                if (context == CONTEXT_EXCLUSIVE)
                {
                    // One of the blocks goes to the exclusive pool
                    assert(type.free[CONTEXT_EXCLUSIVE] == 0);
                    type.free[CONTEXT_EXCLUSIVE]++;

                    // The rest to the normal pool
                    size--;
                }
                type.free[CONTEXT_NORMAL] += size;
                type.list[index].first = 0;
            }
        }
    }
}

void DRISC::RAUnit::Cmd_Info(ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
        "The Register Allocation Unit is the component that manages the allocation\n"
        "data for the register file. It simply maintains an administration that\n"
        "indicates which registers are allocated and which are not.\n"
        "This component is used during family creation to allocate a block of\n"
        "registers for the new family.\n\n"
        "Supported operations:\n"
        "- inspect <component>\n"
        "  Reads the administration from the register allocation unit. Use this to\n"
        "  quickly see which registers are allocated to which family.\n";
}

void DRISC::RAUnit::Cmd_Read(ostream& out, const vector<string>& /*arguments*/) const
{
    static const char* TypeNames[NUM_REG_TYPES] = {"Integer", "Float"};

    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        const TypeInfo& type = m_types[i];

        out << TypeNames[i] << " registers (" << dec << type.blockSize << " registers per block):" << endl;
        for (size_t next, entry = 0; entry < type.list.size(); entry = next)
        {
            out << hex << setfill('0');
            out << "0x" << setw(4) << entry * type.blockSize << " - ";

            if (type.list[entry].first != 0) {
                next = entry + type.list[entry].first;
                out << "0x" << setw(4) << (next * type.blockSize) - 1 << ": Allocated to F" << dec << type.list[entry].second << endl;
            } else {
                for (next = entry + 1; next < type.list.size() && type.list[next].first == 0; ++next) {}
                out << "0x" << setw(4) << (next * type.blockSize) - 1 << ": Free" << endl;
            }
        }
        out << endl;
    }

    for (size_t i = 0; i < NUM_REG_TYPES; ++i)
    {
        static const char* TypeName[NUM_REG_TYPES] = {"int", "flt"};

        const TypeInfo& type = m_types[i];
        out << endl
            << "Free " << TypeName[i] << " register contexts: " << dec
            << type.free[CONTEXT_NORMAL] << " normal, "
            << type.free[CONTEXT_RESERVED] << " reserved, "
            << type.free[CONTEXT_EXCLUSIVE] << " exclusive"
            << endl;
    }
}

}
