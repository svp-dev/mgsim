#ifndef ZLCDMA_DIRECTORY_H
#define ZLCDMA_DIRECTORY_H

#include "Node.h"
#include <sim/inspect.h>
#include <arch/BankSelector.h>

#include <list>

class Config;

namespace Simulator
{

class ZLCDMA::DirectoryTop : public ZLCDMA::Node
{
protected:
    friend class ZLCDMA;
    friend class ZLCDMA::Directory;
    DirectoryTop(const std::string& name, ZLCDMA& parent, Clock& clock);
};

class ZLCDMA::DirectoryBottom : public ZLCDMA::Node
{
protected:
    friend class ZLCDMA;
    friend class ZLCDMA::Directory;
    DirectoryBottom(const std::string& name, ZLCDMA& parent, Clock& clock);
};

class ZLCDMA::Directory : public ZLCDMA::Object, public Inspect::Interface<Inspect::Read>
{
public:
    struct Line
    {
        bool         valid;
        MemAddr      tag;
        unsigned int tokens;     ///< Tokens in the caches in the group
        Line() : valid(false), tag(0), tokens(0) {}
    };

protected:
    friend class ZLCDMA;
    ZLCDMA::DirectoryBottom m_bottom;
    ZLCDMA::DirectoryTop    m_top;

private:
    typedef ZLCDMA::Node::Message Message;

    IBankSelector&      m_selector;
    ArbitratedService<CyclicArbitratedPort> p_lines;      ///< Arbitrator for access to the lines
    size_t              m_assoc;      ///< Number of lines in a set
    size_t              m_sets;       ///< Number of sets
    std::vector<Line>   m_lines;      ///< The cache lines
    size_t              m_lineSize;   ///< The size of a cache-line
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

    Directory(const std::string& name, ZLCDMA& parent, Clock& clock, CacheID firstCache, Config& config);

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
