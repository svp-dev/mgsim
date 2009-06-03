#ifndef ICACHE_H
#define ICACHE_H

#include "kernel.h"
#include "Memory.h"

class Config;

namespace Simulator
{

class Processor;
class Allocator;

class ICache : public IComponent
{
public:
	// A Cache-line
    struct Line
    {
		bool		  used;			// Line used or empty?
        MemAddr       tag;			// Address tag
        char*         data;			// The line data
        CycleNo       access;		// Last access time (for LRU replacement)
		bool          creation;		// Is the family creation process waiting on this line?
        ThreadQueue	  waiting;		// Threads waiting on this line
		unsigned long references;	// Number of references to this line
		bool          fetched;		// To verify that we're actually reading a fetched line
	};

    ICache(Processor& parent, const std::string& name, Allocator& allocator, const Config& config);
    ~ICache();

    Result Fetch(MemAddr address, MemSize size, CID& cid);				// Initial family line fetch
    Result Fetch(MemAddr address, MemSize size, TID& tid, CID& cid);	// Thread code fetch
    bool   Read(CID cid, MemAddr address, void* data, MemSize size) const;
    bool   ReleaseCacheLine(CID bid);
    bool   IsEmpty() const;
    bool   OnMemoryReadCompleted(const MemData& data);

    // Admin information
    size_t      GetAssociativity() const { return m_assoc; }
    size_t      GetLineSize()      const { return m_lineSize; }
    size_t      GetNumLines()      const { return m_lines.size(); }
    size_t      GetNumSets()       const { return m_lines.size() / m_assoc; }
    const Line& GetLine(size_t i)  const { return m_lines[i];   }
    uint64_t    GetNumHits()       const { return m_numHits; }
    uint64_t    GetNumMisses()     const { return m_numMisses; }

private:
    Result Fetch(MemAddr address, MemSize size, TID* tid, CID* cid);
    Result FindLine(MemAddr address, Line* &line);

    Processor&          m_parent;
	Allocator&          m_allocator;
    std::vector<Line>   m_lines;
	char*               m_data;
    uint64_t            m_numHits;
    uint64_t            m_numMisses;
    size_t              m_lineSize;
    size_t              m_assoc;
    size_t              m_assocLog;
};

}
#endif

