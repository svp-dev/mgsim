#ifndef COMA_DIRECTORY_H
#define COMA_DIRECTORY_H

#include "Node.h"
#include <queue>
#include <set>

class Config;

namespace Simulator
{

class COMA::DirectoryTop : public COMA::Node
{
protected:
    DirectoryTop(const std::string& name, COMA& parent, Clock& clock, const Config& config);
};

class COMA::DirectoryBottom : public COMA::Node
{
protected:
    DirectoryBottom(const std::string& name, COMA& parent, Clock& clock, const Config& config);
};

class COMA::Directory : public COMA::DirectoryBottom, public COMA::DirectoryTop
{
public:
    struct Line
    {
        bool         valid;  ///< Valid line?
        MemAddr      tag;    ///< Tag of this line
        unsigned int tokens; ///< Tokens in this ring
    };
    
private:
    ArbitratedService<CyclicArbitratedPort> p_lines;      ///< Arbitrator for access to the lines
    std::vector<Line>   m_lines;      ///< The cache lines
    size_t              m_lineSize;   ///< The size of a cache-line
    size_t              m_assoc;      ///< Number of lines in a set
    size_t              m_sets;       ///< Number of sets
    CacheID             m_firstCache; ///< ID of first cache in the ring
    CacheID             m_lastCache;  ///< ID of last cache in the ring
    
    // Processes
    Process p_InBottom;
    Process p_InTop;

    Line* FindLine(MemAddr address);
    Line* AllocateLine(MemAddr address);
    bool  OnMessageReceivedBottom(Message* msg);
    bool  OnMessageReceivedTop(Message* msg);
    bool  IsBelow(CacheID id) const;
    
    // Processes
    Result DoInBottom();
    Result DoOutBottom();
    Result DoInTop();
    Result DoOutTop();

public:
    const Line* FindLine(MemAddr address) const;

    Directory(const std::string& name, COMA& parent, Clock& clock, CacheID firstCache, CacheID lastCache, const Config& config);
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
