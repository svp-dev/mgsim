#ifndef ZLCOMA_ROOTDIRECTORY_H
#define ZLCOMA_ROOTDIRECTORY_H

#include "Directory.h"
#include "mem/DDR.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class ZLCOMA::RootDirectory : public ZLCOMA::DirectoryBottom, public DDRChannel::ICallback, public Inspect::Interface<Inspect::Read>
{
public:
    struct Line
    {
        bool         valid;             // is this line used?
        MemAddr      tag;               // tag of the address of the line that's stored
        bool         loading;           // the cacheline is being loaded from off-chip
        bool         data;              // has the line been loaded into the system?
        unsigned int tokens;            // the number of tokens that the directory itself has
        bool         priority;          // represent the priority token
        std::queue<Message*> requests;  // Suspended requests
    };

private:
    std::vector<Line> m_lines;      ///< The cache lines
    size_t            m_lineSize;   ///< The size of a cache-line
    size_t            m_assoc;      ///< Number of lines in a set
    size_t            m_sets;       ///< Number of sets
    size_t            m_numTokens;  ///< Number of tokens per line in the COMA system
    size_t            m_id;         ///< Which root directory we are (0 <= m_id < m_numRoots)
    size_t            m_numRoots;   ///< Number of root directories on the top-level ring

    ArbitratedService<> p_lines;      ///< Arbitrator for lines and output

    DDRChannel*       m_memory;    ///< DDR memory channel
    Buffer<Message*>  m_requests;  ///< Requests to memory
    Buffer<Message*>  m_responses; ///< Responses from memory
    Flag              m_memready;  ///< Memory ready to receive another request
    Message*          m_activeMsg; ///< Currently active message to the memory

	std::queue<Line*>    m_activelines;

    // Processes
    Process p_Incoming;
    Process p_Requests;
    Process p_Responses;

    Line* FindLine(MemAddr address);
    Line* GetEmptyLine(MemAddr address);
    bool  OnMessageReceived(Message* msg);
    bool  OnReadCompleted(MemAddr address, const MemData& data);

    // Processes
    Result DoIncoming();
    Result DoRequests();
    Result DoResponses();

    // Statistics
    uint64_t          m_nreads;
    uint64_t          m_nwrites;

public:
    RootDirectory(const std::string& name, ZLCOMA& parent, Clock& clock, VirtualMemory& memory, size_t numTokens, size_t id, size_t numRoots, const DDRChannelRegistry& ddr, Config& config);

    // Administrative
    const Line* FindLine(MemAddr address) const;

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;

    // Statistics
    void GetMemoryStatistics(uint64_t& nreads_ext, uint64_t& nwrites_ext) const
    {
        nreads_ext = m_nreads;
        nwrites_ext = m_nwrites;
    }
};

}

#endif

