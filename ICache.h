#ifndef ICACHE_H
#define ICACHE_H

#include "kernel.h"
#include "functions.h"
#include "Memory.h"

namespace Simulator
{

class Processor;
class Allocator;

class ICache : public IComponent
{
public:
	// Instruction Cache Configuration
	struct Config
	{
		size_t assoc;
		size_t sets;
		size_t lineSize;
	};

	// A Cache-line
    struct Line
    {
		bool		  used;			// Line used or empty?
        MemAddr       tag;			// Address tag
        char*         data;			// The line data
        CycleNo       access;		// Last access time
		bool          creation;		// Is the family creation process waiting on this line?
        ThreadQueue	  waiting;		// Threads waiting on this line
		unsigned long references;	// Number of references to this line
		bool          fetched;		// To verify that we're actually reading a fetched line
	};

    ICache(Processor& parent, const std::string& name, Allocator& allocator, const Config& config);
    ~ICache();

    Result fetch(MemAddr address, MemSize size, CID& cid);				// Initial family line fetch
    Result fetch(MemAddr address, MemSize size, TID& tid, CID& cid);	// Thread code fetch
    bool   read(CID cid, MemAddr address, void* data, MemSize size) const;
    bool   releaseCacheLine(CID bid);

    bool onMemoryReadCompleted(const MemData& data);

    // Ports
    ArbitratedWriteFunction p_request;

    // Admin information
    size_t      getAssociativity() const { return m_assoc; }
    size_t      getLineSize()      const { return m_lineSize; }
    size_t      getNumLines()      const { return m_lines.size(); }
    size_t      getNumSets()       const { return m_lines.size() / m_assoc; }
    const Line& getLine(size_t i)  const { return m_lines[i];   }
    uint64_t    getNumHits()       const { return m_numHits; }
    uint64_t    getNumMisses()     const { return m_numMisses; }

private:
    Result fetch(MemAddr address, MemSize size, TID* tid, CID* cid);
    Result findLine(MemAddr address, Line* &line);

    Processor&          m_parent;
	Allocator&          m_allocator;
    size_t              m_lineSize;
    std::vector<Line>   m_lines;
	char*               m_data;
    uint64_t            m_numHits;
    uint64_t            m_numMisses;
    size_t              m_assoc;
    size_t              m_assocLog;
};

}
#endif

