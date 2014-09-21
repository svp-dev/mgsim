// -*- c++ -*-
#ifndef CDMA_DIRECTORY_H
#define CDMA_DIRECTORY_H

#include "Node.h"
#include <sim/inspect.h>

#include <vector>
#include <map>

class Config;

namespace Simulator
{


class CDMA::DirectoryTop : public CDMA::Node
{
protected:
    friend class OneLevelCDMA;
    friend class TwoLevelCDMA;
    friend class CDMA::Directory;
    DirectoryTop(const std::string& name, CDMA& parent, Clock& clock, size_t& numLines, Config& config);
    size_t GetNumLines() const override;
    size_t& m_numLines;
};

class CDMA::DirectoryBottom : public CDMA::Node
{
protected:
    friend class OneLevelCDMA;
    friend class TwoLevelCDMA;
    friend class CDMA::Directory;
    DirectoryBottom(const std::string& name, CDMA& parent, Clock& clock, Config& config);
};

class CDMA::Directory : public CDMA::Object, public Inspect::Interface<Inspect::Read>
{
protected:
    friend class CDMA;
    friend class OneLevelCDMA;
    friend class TwoLevelCDMA;
    CDMA::DirectoryBottom m_bottom;
    CDMA::DirectoryTop    m_top;

private:
    typedef CDMA::Node::Message Message;

    ArbitratedService<CyclicArbitratedPort> p_lines;      ///< Arbitrator for access to the lines

    std::map<MemAddr, size_t> m_dir;  ///< The directory: tag -> tokens

    size_t              m_maxNumLines; ///< Maximum number of lines to store
    NodeID              m_firstNode;  ///< ID of first node in the subring
    NodeID              m_lastNode;   ///< ID of last node in the subring

    // Processes
    Process p_InBottom;
    Process p_InTop;

    size_t* FindLine(MemAddr address);
    size_t* AllocateLine(MemAddr address);
    bool  OnMessageReceivedBottom(Message* msg);
    bool  OnMessageReceivedTop(Message* msg);
    bool  IsBelow(NodeID id) const;

    // Processes
    Result DoInBottom();
    Result DoOutBottom();
    Result DoInTop();
    Result DoOutTop();

public:
    Directory(const std::string& name, CDMA& parent, Clock& clock, Config& config);

    // Connect directory to caches
    void ConnectRing(Node* first, Node* last);
    // Initialize after all caches have been connected
    void Initialize();

    size_t GetMaxNumLines() const { return m_maxNumLines; }

    void Cmd_Info(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
