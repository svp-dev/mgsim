// -*- c++ -*-
#ifndef SIM_BINARY_SAMPLER_H
#define SIM_BINARY_SAMPLER_H

#include <ostream>
#include <string>
#include <vector>
#include <utility>
#include <cstddef>

class Config;

namespace Simulator
{
    class VariableRegistry;

    // BinarySampler: used by Monitor to quickly serialize scalar
    // variables to a binary format.
    class BinarySampler
    {
    private:
        typedef std::vector<std::pair<const char*, size_t> > vars_t;

        size_t   m_datasize;          ///< The record size in bytes
        vars_t   m_vars;              ///< The variables to sample

        const VariableRegistry& m_registry; ///< The related registry

    public:

        // Create a binary sampler
        BinarySampler(const VariableRegistry& registry);

        // Select a set of variables and dump a serialization
        // header.
        void SelectVariables(std::ostream& os, const Config& config,
                             const std::vector<std::string>& pats);

        // Perform the sampling
        void SampleToBuffer(char *buf) const
        {
            for (auto& i : m_vars)
                for (size_t j = 0; j < i.second; ++j)
                    *buf++ = i.first[j];
        }

        size_t GetBufferSize() const { return m_datasize; }

    };


}

#endif
