// -*- c++ -*-
#ifndef BANK_SELECTION_H
#define BANK_SELECTION_H

#include <sim/kernel.h>
#include <arch/simtypes.h>

namespace Simulator
{

    class IBankSelector {
    public:

        // cache line address to cache tag + bank index
        virtual void Map(MemAddr address, MemAddr& tag, size_t& index) = 0;
        // cache tag + bank index to cache line address
        virtual MemAddr Unmap(MemAddr tag, size_t index) = 0;

        virtual const std::string& GetName() const = 0;
        virtual size_t GetNumBanks() const = 0;
        virtual ~IBankSelector() {};

        static IBankSelector* makeSelector(Object& parent, const std::string& name, size_t numBanks);
    };


}


#endif
