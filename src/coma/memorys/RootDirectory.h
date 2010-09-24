#ifndef ZLCOMA_ROOTDIRECTORY_H
#define ZLCOMA_ROOTDIRECTORY_H

#include "Directory.h"
#include "DDR.h"
#include "evicteddirlinebuffer.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class DDRChannel;

class ZLCOMA::RootDirectory : public ZLCOMA::DirectoryBottom, public DDRChannel::ICallback
{
public:
    struct Line
    {
        bool         valid;             // is this line used?
        MemAddr      tag;               // tag of the address of the line that's stored
        bool         reserved;          // the cacheline cannot be processed immediately
        unsigned int tokens;            // the number of tokens that the directory itself has
        bool         priority;          // represent the priority token
        size_t       source;            // Source of the pending read
        std::queue<Message*> requests;  // Suspended requests
    };

private:
    std::vector<Line> m_lines;      ///< The cache lines
    size_t            m_lineSize;   ///< The size of a cache-line
    size_t            m_assoc;      ///< Number of lines in a set
    size_t            m_sets;       ///< Number of sets
    size_t            m_numTokens;

    ArbitratedService<> p_lines;      ///< Arbitrator for lines and output

    DDRChannel*       m_memory;    ///< DDR memory channel
    Buffer<Message*>  m_requests;  ///< Requests to memory
    Buffer<Message*>  m_responses; ///< Responses from memory
    Flag              m_memready;  ///< Memory ready to receive another request
    Message*          m_activeMsg; ///< Currently active message to the memory

	std::queue<Line*>    m_activelines;
    EvictedDirLineBuffer m_evictedlinebuffer;

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

public:
public:
    RootDirectory(const std::string& name, ZLCOMA& parent, Clock& clock, VirtualMemory& memory, size_t numTokens, const Config& config);
    ~RootDirectory();

    // Administrative
    const Line* FindLine(MemAddr address) const;

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}

#endif

