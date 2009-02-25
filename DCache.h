#ifndef DCACHE_H
#define DCACHE_H

#include <queue>
#include "kernel.h"
#include "functions.h"
#include "Memory.h"

namespace Simulator
{

class Processor;
class Allocator;
class FamilyTable;
class RegisterFile;

class DCache : public IComponent
{
public:
	struct Config
	{
		size_t assoc;    ///< Cache associativity.
		size_t sets;     ///< Number of sets in the cace.
		size_t lineSize; ///< Size of a cache line, in bytes.
	};

    /// Represents a queue of cache-lines
    struct LineQueue
    {
        CID head;   ///< First line in the queue.
        CID tail;   ///< Last line in the queue.
    };

    /// The state of a cache-line
    enum LineState
    {
        LINE_EMPTY,      ///< Line is empty.
        LINE_LOADING,    ///< Line is being loaded.
        LINE_PROCESSING, ///< Line is full and being processed.
        LINE_INVALID,    ///< Line is loading or processing, but invalid.
        LINE_FULL        ///< Line is full.
    };

    struct Line
    {
        LineState   state;  ///< The line state.
        MemAddr     tag;    ///< The address tag.
        char*       data;   ///< The data in this line.
        CycleNo     access; ///< Last access time of this line (for LRU).
        CID         next;   ///< Next cache-line in to-be-processed list.
		RegAddr		waiting;///< First register waiting on this line.
    };

    DCache(Processor& parent, const std::string& name, Allocator& allocator, FamilyTable& familyTable, RegisterFile& regFile, const Config& config);
    ~DCache();

    Result Read (MemAddr address, void* data, MemSize size, LFID fid, RegAddr* reg);
    Result Write(MemAddr address, void* data, MemSize size, LFID fid, TID tid);

    // IMemoryCallback
    bool OnMemoryReadCompleted(const MemData& data);
    bool OnMemoryWriteCompleted(const MemTag& tag);
    bool OnMemorySnooped(MemAddr addr, const MemData& data);

    // Component
    Result OnCycleWritePhase(unsigned int stateIndex);

    // Ports
    ArbitratedWriteFunction p_request;

    // Admin information
    size_t GetAssociativity() const { return m_config.assoc; }
    size_t GetLineSize()      const { return m_config.lineSize; }
    size_t GetNumLines()      const { return m_lines.size(); }
    size_t GetNumSets()       const { return m_lines.size() / m_config.assoc; }
    size_t GetNumPending()    const { return m_numWaiting; }

    const Line& GetLine(size_t i) const { return m_lines[i];  }
    uint64_t    GetNumHits()      const { return m_numHits;   }
    uint64_t    GetNumMisses()    const { return m_numMisses; }

private:
    Result FindLine(MemAddr address, Line* &line, bool check_only);

    Processor&           m_parent;      ///< Parent processor.
    Allocator&			 m_allocator;   ///< Allocator component.
	FamilyTable&		 m_familyTable; ///< Family table .
	RegisterFile&		 m_regFile;     ///< Register File.

    std::vector<Line>    m_lines;       ///< The cache-lines.
    Config               m_config;      ///< Configuration data.
    unsigned int         m_numWaiting;  ///< Number of pending requests.
    LineQueue            m_returned;    ///< Returned cache-lines waiting to be processed.
    uint64_t             m_numHits;     ///< Number of hits so far.
    uint64_t             m_numMisses;   ///< Number of misses so far.
};

}
#endif

