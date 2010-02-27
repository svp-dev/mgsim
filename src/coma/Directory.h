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
    DirectoryTop(const std::string& name, COMA& parent);
};

class COMA::DirectoryBottom : public COMA::Node
{
protected:
    DirectoryBottom(const std::string& name, COMA& parent);
};

class COMA::Directory : public COMA::DirectoryBottom, public COMA::DirectoryTop
{
public:
    struct Line
    {
        bool    valid;  ///< Valid line?
        MemAddr tag;    ///< Tag of this line
    };
    
private:
    ArbitratedService p_lines;      ///< Arbitrator for access to the lines
    std::vector<Line> m_lines;      ///< The cache lines
    size_t            m_lineSize;   ///< The size of a cache-line
    size_t            m_assoc;      ///< Number of lines in a set
    size_t            m_sets;       ///< Number of sets
    bool              m_topLevel;   ///< Is this a directory in the top-level ring?
    size_t            m_numCaches;  ///< The number of caches that lie under the directory
    
    // Processes
    Process p_InPrevBottom;
    Process p_InNextBottom;
    Process p_OutNextBottom;
    Process p_OutPrevBottom;
    Process p_InPrevTop;
    Process p_InNextTop;
    Process p_OutNextTop;
    Process p_OutPrevTop;    

    Line* FindLine(MemAddr address);
    Line* AllocateLine(MemAddr address);
    bool  OnRequestReceivedBottom(Message* msg);
    bool  OnRequestReceivedTop(Message* msg);
    bool  OnResponseReceivedTop(Message* msg);
    bool  OnResponseReceivedBottom(Message* msg);
    
    // Processes
    Result DoInPrevBottom();
    Result DoInNextBottom();
    Result DoOutNextBottom();
    Result DoOutPrevBottom();
    Result DoInPrevTop();
    Result DoInNextTop();
    Result DoOutNextTop();
    Result DoOutPrevTop();    

public:
    const Line* FindLine(MemAddr address) const;

    // numCaches is the number of caches below this directory
    Directory(const std::string& name, COMA& parent, bool top_level, size_t numCaches, const Config& config);
    
    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
