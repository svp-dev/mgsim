#include "RandomBankedMemory.h"
#include <iostream>

namespace Simulator
{

size_t RandomBankedMemory::GetBankFromAddress(MemAddr address) const
{
    address &= ~(m_cachelineSize - 1);
#if MEMSIZE_MAX >= 4294967296
    address = address ^ ((address >> 32) | (address << (sizeof(address)*8-32)));
#endif
    address = address ^ ((address >> 16) | (address << (sizeof(address)*8-16)));
    address = address ^ ((address >> 8) | (address << (sizeof(address)*8-8)));
    address = address ^ ((address >> 4) | (address << (sizeof(address)*8-4)));
    return address % m_banks.size();
}

RandomBankedMemory::RandomBankedMemory(const std::string& name, Object& parent, Clock& clock, Config& config)
    : BankedMemory(name, parent, clock, config)
{
}

void RandomBankedMemory::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Random Banked Memory represents a switched memory network between P\n"
    "processors and N memory banks. Requests are sequentialized on each bank and the\n"
    "cache line-to-bank mapping uses a hash function to pseudo-randomize the mapping\n"
    "to avoid structural bank conflicts.\n\n"
    "Supported operations:\n"
    "- info <component>\n"
    "  Displays the currently reserved and allocated memory ranges\n\n"
    "- inspect <component> <start> <size>\n"
    "  Reads the specified number of bytes of raw data from memory from the\n"
    "  specified address\n\n"
    "- inspect <component> requests\n"
    "  Reads the banks' requests buffers and queues\n";
}

}
