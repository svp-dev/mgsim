#ifndef COMA_ROOTDIRECTORY_H
#define COMA_ROOTDIRECTORY_H

#include "Directory.h"
#include "DDR.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class DDRChannel;

class COMA::RootDirectory : public COMA::DirectoryBottom, public DDRChannel::ICallback
{
public:    
    enum LineState
    {
        LINE_EMPTY,
        LINE_LOADING,
        LINE_FULL
    };
    
    struct Line
    {
        LineState    state;    ///< State of the line
        MemAddr      tag;      ///< Tag of this line
        unsigned int hops;     ///< Loading: hops to the cache that wants the reply
    };

private:
    struct MemRequest
    {
        MemAddr address;
        MemData data;
    };
    
    ArbitratedService p_lines;      ///< Arbitrator for access to the lines
    std::vector<Line> m_lines;      ///< The cache lines
    size_t            m_lineSize;   ///< The size of a cache-line
    size_t            m_assoc;      ///< Number of lines in a set
    size_t            m_sets;       ///< Number of sets
    size_t            m_numCaches;
    DDRChannel*       m_memory;     ///< DDR memory channel

    Buffer<MemRequest> m_incoming;    ///< To memory
    Buffer<Message*>   m_outgoing;    ///< From memory
    
    // Processes
    Process p_InPrevBottom;
    Process p_InNextBottom;
    Process p_OutNextBottom;
    Process p_OutPrevBottom;
    Process p_Incoming;
    Process p_Outgoing;
    
    Line* FindLine(MemAddr address, bool check_only);
    bool  OnRequestReceived(Message* msg);
    bool  OnResponseReceived(Message* msg);
    bool  OnReadCompleted(MemAddr address, const MemData& data);
    
    // Processes
    Result DoInPrevBottom();
    Result DoInNextBottom();
    Result DoOutNextBottom();
    Result DoOutPrevBottom();
    Result DoIncoming();
    Result DoOutgoing();

public:
    RootDirectory(const std::string& name, COMA& parent, VirtualMemory& memory, size_t numCaches, const Config& config);
    ~RootDirectory();
    
    // Administrative
    const Line* FindLine(MemAddr address) const;

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
