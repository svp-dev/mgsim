#include "RandomBankedMemory.h"
#include <iostream>

namespace Simulator
{

size_t RandomBankedMemory::GetBankFromAddress(MemAddr address) const
{
    // We work on whole cache lines
    address /= m_cachelineSize;

    uint64_t hash = (31 * address / m_banks.size() / 32);

    return (size_t)((hash + address) % m_banks.size());
}

RandomBankedMemory::RandomBankedMemory(Object* parent, Kernel& kernel, const std::string& name, const Config& config)
    : BankedMemory(parent, kernel, name, config)
{
}

void RandomBankedMemory::Cmd_Help(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Random Banked Memory represents a switched memory network between P\n"
    "processors and N memory banks. Requests are sequentialized on each bank and the\n"
    "cache line-to-bank mapping uses a hash function to pseudo-randomize the mapping\n"
    "to avoid structural bank conflicts.\n\n"
    "Supported operations:\n"
    "- info <component>\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- read <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- read <component> requests\n"
    "  Reads the banks' requests buffers and queues\n";
}

}
