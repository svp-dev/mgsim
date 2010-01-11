#ifndef RANDOMBANKEDMEMORY_H
#define RANDOMBANKEDMEMORY_H

#include "BankedMemory.h"

namespace Simulator
{

// The RandomBankedMemory is a small and trivial extension to BankedMemory
class RandomBankedMemory : public BankedMemory
{
    size_t GetBankFromAddress(MemAddr address) const;
        
public:
    RandomBankedMemory(const std::string& name, Object& parent, const Config& config);
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif

