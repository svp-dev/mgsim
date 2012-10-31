#include "BankSelector.h"

#include <sim/log2.h>
#include <sim/except.h>

/*
  Mapping of cache line addresses to cache set/bank indices
  is configurable for most banked memory structures.

  The choice of a mapping is a trade-off between the hardware
  implementation cost (both in logic complexity and the impact on the
  critical path, which may increase cycle time), and the obtained
  balance + concentration on the pattern of memory accesses.

  For an in-depth analysis of the issues refer to:

  http://ieeexplore.ieee.org/xpls/abs_all.jsp?arnumber=1410085&tag=1
  http://portal.acm.org/citation.cfm?id=1072477
  M. Kharbutli, K. Irwin, Y. Solihin, and J. Lee, “Using prime numbers
  for cache indexing to eliminate conflict misses,” in Proceedings of
  the 10th International Symposium on High Performance Computer
  Architecture (HPCA’04), (Washington, DC, USA), pp. 288–299, IEEE
  Computer Society, 2004.

  http://portal.acm.org/citation.cfm?id=263599
  A. Gonz ́alez, M. Valero, N. Topham, and J. M. Parcerisa,
  “Eliminating cache conflict misses through xor-based placement
  functions,” in Proceedings of the 11th International Conference on
  Supercomputing (ICS’97), (New York, NY, USA), pp. 76–83, ACM, 1997.

  http://portal.acm.org/citation.cfm?id=165152
  A. Seznec, “A case for two-way skewed-associative caches,” in
  Proceedings of the 20th annual International Symposium on Computer
  Architecture, ISCA ’93, (New York, NY, USA), pp. 169–178, ACM, 1993.

  http://portal.acm.org/citation.cfm?id=115961
  B. R. Rau, “Pseudo-randomly interleaved memory,” SIGARCH
  Comput. Archit. News, vol. 19, pp. 74–83, April 1991.

  http://portal.acm.org/citation.cfm?id=110396
  R. Raghavan and J. P. Hayes, “On randomly interleaved memories,” in
  Proceedings of the 1990 ACM/IEEE conference on Supercomputing,
  Supercomputing ’90, (Los Alamitos, CA, USA), pp. 49–58, IEEE
  Computer Society Press, 1990.

*/

namespace Simulator
{
    class SelectorBase : public IBankSelector
    {
    protected:
        std::string m_name;
        size_t m_numBanks;
    public:
        SelectorBase(const std::string& name, size_t numBanks)
            : m_name(name),
              m_numBanks(numBanks)
        {}
        std::string GetName() const { return m_name; }
        size_t GetNumBanks() const { return m_numBanks; }
    };

    // ZeroSelector: selects always bank 0
    // (suitable for memories with only one bank, or for testing the effects of conflicts)
    class ZeroSelector : public SelectorBase
    {
    public:
        ZeroSelector(size_t numBanks)
            : SelectorBase("bank 0 only", numBanks)
        {}

        void Map(MemAddr address, MemAddr& tag, size_t& index)
        {
            tag = address;
            index = 0;
        }
        MemAddr Unmap(MemAddr tag, size_t /*index*/)
        {
            return tag;
        }
    };

    // DirectSelector: selects bank based on the least significant bits only
    // simple to implement, but poor balance for strides larger than the
    // index capacity
    class DirectSelector : public SelectorBase
    {
    public:
        DirectSelector(size_t numBanks)
            : SelectorBase("direct (div+mod)", numBanks)
        {}
        void Map(MemAddr address, MemAddr& tag, size_t& index)
        {
            tag = address / m_numBanks;
            index = address % m_numBanks;
        }
        MemAddr Unmap(MemAddr tag, size_t index)
        {
            return (tag * m_numBanks) + index;
        }
    };

    // Same as above using simple binary arithmetic.
    class DirectSelectorBinary : public SelectorBase
    {
        size_t m_bankmask;
        size_t m_bankshift;
    public:
        DirectSelectorBinary(size_t numBanks)
            : SelectorBase("direct (shift+and)", numBanks),
              m_bankmask(numBanks - 1),
              m_bankshift(ilog2(numBanks))
        {}
        void Map(MemAddr address, MemAddr& tag, size_t& index)
        {
            tag = address >> m_bankshift;
            index = address & m_bankmask;
        }
        MemAddr Unmap(MemAddr tag, size_t index)
        {
            return (tag << m_bankshift) | index;
        }
    };

    // RotationMix4: a naive attempt at randomization
    // (legacy for MGSim: is used to implement the non-realistic random banked memory)
    // is expensive to implement in hardware due to the sequence of 4 rotations
    class RotationMix4 : public SelectorBase
    {
    public:
        RotationMix4(size_t numBanks)
            : SelectorBase("4-bit full rotation mix", numBanks)
        {}
        void Map(MemAddr address, MemAddr& tag, size_t& index)
        {
            tag = address;
#if MEMSIZE_MAX >= 4294967296
            address = address ^ ((address >> 32) | (address << (sizeof(address)*8-32)));
#endif
            address = address ^ ((address >> 16) | (address << (sizeof(address)*8-16)));
            address = address ^ ((address >> 8) | (address << (sizeof(address)*8-8)));
            address = address ^ ((address >> 4) | (address << (sizeof(address)*8-4)));
            index = address % m_numBanks;
        }
        MemAddr Unmap(MemAddr tag, size_t /*index*/)
        {
            return tag;
        }
    };

    // RightXOR: uses a XOR on the two low order blocks of index-sized bits in the address
    // Introduced by Gonzales et al in ICS'97.
    class RightXOR : public SelectorBase
    {
    public:
        RightXOR(size_t numBanks)
            : SelectorBase("(addr XOR (addr / numbanks)) % numbanks", numBanks)
        {}
        void Map(MemAddr address, MemAddr& tag, size_t& index)
        {
            tag = address;
            index = (address ^ (address / m_numBanks)) % m_numBanks;
        }
        MemAddr Unmap(MemAddr tag, size_t /*index*/)
        {
            return tag;
        }
    };

    // RightAdd: generalization of RightXOR with integer arithmetic
    class RightAdd : public SelectorBase
    {
    public:
        RightAdd(size_t numBanks)
            : SelectorBase("(addr + (addr / numbanks)) % numbanks", numBanks)
        {}
        void Map(MemAddr address, MemAddr& tag, size_t& index)
        {
            tag = address;
            index = (address + (address / m_numBanks)) % m_numBanks;
        }
        MemAddr Unmap(MemAddr tag, size_t /*index*/)
        {
            return tag;
        }
    };

    // XORFold: generalization of RightXOR to use all bits in the address
    // (suitable for address partitioning with core/thread identification in high bits)
    class XORFold : public SelectorBase
    {
    public:
        XORFold(size_t numBanks)
            : SelectorBase("XOR fold of numbanks-sized sub-words", numBanks)
        {}
        void Map(MemAddr address, MemAddr& tag, size_t& index)
        {
            tag = address;
            MemAddr result = 0;
            do
            {
                result ^= address;
                address /= m_numBanks;
            }
            while (address > m_numBanks);
            index = result % m_numBanks;
        }
        MemAddr Unmap(MemAddr tag, size_t /*index*/)
        {
            return tag;
        }
    };

    // AddFold: generalization of XORFold with integer arithmetic
    class AddFold : public SelectorBase
    {
    public:
        AddFold(size_t numBanks)
            : SelectorBase("add fold of numbanks-sized sub-words", numBanks)
        {}
        void Map(MemAddr address, MemAddr& tag, size_t& index)
        {
            tag = address;
            MemAddr result = 0;
            do
            {
                result += address;
                address /= m_numBanks;
            }
            while (address > m_numBanks);
            index = result % m_numBanks;
        }
        MemAddr Unmap(MemAddr tag, size_t /*index*/)
        {
            return tag;
        }
    };

    IBankSelector* IBankSelector::makeSelector(Object& parent, const std::string& name, size_t numBanks)
    {
        if (numBanks == 1 || name == "ZERO")
            return new ZeroSelector(numBanks);

        if (name == "DIRECT")
        {
            return IsPowerOfTwo(numBanks) ? (IBankSelector*)new DirectSelectorBinary(numBanks) : (IBankSelector*)new DirectSelector(numBanks);
        }
        else if (name == "RMIX")    { return new RotationMix4(numBanks); }
        else if (name == "XORFOLD") { return new XORFold(numBanks); }
        else if (name == "ADDFOLD") { return new AddFold(numBanks); }
        else if (name == "XORLSB")  { return new RightXOR(numBanks); }
        else if (name == "ADDLSB")  { return new RightAdd(numBanks); }
        {
            throw exceptf<InvalidArgumentException>(parent, "Unknown banking strategy: %s", name.c_str());
        }
    }

}

