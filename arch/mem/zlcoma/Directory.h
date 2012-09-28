#ifndef ZLCOMA_DIRECTORY_H
#define ZLCOMA_DIRECTORY_H

#include "Node.h"
#include <sim/inspect.h>
#include <arch/BankSelector.h>

#include <list>

class Config;

namespace Simulator
{

class ZLCOMA::DirectoryTop : public ZLCOMA::Node
{
protected:
    friend class ZLCOMA;
    friend class ZLCOMA::Directory;
    DirectoryTop(const std::string& name, ZLCOMA& parent, Clock& clock);
};

class ZLCOMA::DirectoryBottom : public ZLCOMA::Node
{
protected:
    friend class ZLCOMA;
    friend class ZLCOMA::Directory;
    DirectoryBottom(const std::string& name, ZLCOMA& parent, Clock& clock);
};

class ZLCOMA::Directory : public ZLCOMA::Object, public Inspect::Interface<Inspect::Read>
{
public:
    struct Line
    {
        bool         valid;
        MemAddr      tag;
        unsigned int tokens;     ///< Tokens in the caches in the group
    };

protected:
    friend class ZLCOMA;
    ZLCOMA::DirectoryBottom m_bottom;
    ZLCOMA::DirectoryTop    m_top;

private:
    typedef ZLCOMA::Node::Message Message;

    IBankSelector&      m_selector;
    ArbitratedService<CyclicArbitratedPort> p_lines;      ///< Arbitrator for access to the lines
    std::vector<Line>   m_lines;      ///< The cache lines
    size_t              m_lineSize;   ///< The size of a cache-line
    size_t              m_assoc;      ///< Number of lines in a set
    size_t              m_sets;       ///< Number of sets
    size_t              m_numTokens;  ///< Total number of tokens per cache line
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

    bool OnBELRead(Message *);
    bool OnBELAcquireTokens(Message *);
    bool OnBELEviction(Message *);
    bool OnBELDirNotification(Message *);

    // Processes
    Result DoInBottom();
    Result DoOutBottom();
    Result DoInTop();
    Result DoOutTop();

public:
    const Line* FindLine(MemAddr address) const;

    Directory(const std::string& name, ZLCOMA& parent, Clock& clock, CacheID firstCache, Config& config);

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
