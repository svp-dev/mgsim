#ifndef ZLCOMA_DIRECTORY_H
#define ZLCOMA_DIRECTORY_H

#include "Node.h"
#include "evicteddirlinebuffer.h"
#include <list>

class Config;

namespace Simulator
{

// directory implementation has many choices, 
// 1. naive solutioin:
//    with this method, no queue structure is used to store lines form the same location. 
//    so that all the request will go up to the second level ring, 
//    however, root directory will queue to reduce the requests sent out from the chip IO pins
//
//    naive method basically unzips the topology from local directories. 
//    or make a shortcut sometime by bypassing the subring from the local directories
//    everything else works exactly as the single-level ring network
//
//    in this solution, the tokencount in local directory will always remain 0
//
//    1.a with request couting policy
//        in this policy, all the remote requests coming in will report it's tokens to the local directory
//        during entry and departure of the local group
//
// 2. use suspended request queues...

class ZLCOMA::DirectoryTop : public ZLCOMA::Node
{
protected:
    DirectoryTop(const std::string& name, ZLCOMA& parent, Clock& clock);
};

class ZLCOMA::DirectoryBottom : public ZLCOMA::Node
{
protected:
    DirectoryBottom(const std::string& name, ZLCOMA& parent, Clock& clock);
};

class ZLCOMA::Directory : public ZLCOMA::DirectoryBottom, public ZLCOMA::DirectoryTop
{
public:
    struct Line
    {
        bool valid;
        MemAddr tag;

        unsigned int tokencount;    // the number of tokens that the directory itself has
    
        bool priority;              // represent the priority token

        unsigned int nrequestin;    // remote request in local level
                                    // with remote request inside the local level,
                                    // the directory line can still be evicted.
                                    // * DD will not be counted in any way
                                    // * since it might never get out the directory.
                                    // * to avoid inform the directory when consumed, it will not be counted
                                    
        unsigned int nrequestout;   // local requests in global level

        int ntokenline;     // tokens inside on the lines
                            // *** in naive directory scheme
                            // *** the tokencount represent the token directory hold,
                            // *** token group holds the tokens that local caches has
                            // *** the two have no intersections in local directory.

        int ntokenrem;     // tokens inside from remote request
    };

private:
    ArbitratedService<CyclicArbitratedPort> p_lines;      ///< Arbitrator for access to the lines
    std::vector<Line>   m_lines;      ///< The cache lines
    size_t              m_lineSize;   ///< The size of a cache-line
    size_t              m_assoc;      ///< Number of lines in a set
    size_t              m_sets;       ///< Number of sets
    size_t              m_numTokens;  ///< Total number of tokens per cache line
    CacheID             m_firstCache; ///< ID of first cache in the ring
    CacheID             m_lastCache;  ///< ID of last cache in the ring

    // Evicted line buffer
    EvictedDirLineBuffer m_evictedlinebuffer;

    // Processes
    Process p_InBottom;
    Process p_InTop;

    Line* FindLine(MemAddr address);
    Line* GetEmptyLine(MemAddr address);
    bool  OnMessageReceivedBottom(Message* msg);
    bool  OnMessageReceivedTop(Message* msg);
    bool  IsBelow(CacheID id) const;

    bool OnABOAcquireToken(Message *);
    bool OnABOAcquireTokenData(Message *);
    bool OnABODisseminateTokenData(Message *);

    bool OnBELAcquireToken(Message *);
    bool OnBELAcquireTokenData(Message *);
    bool OnBELDisseminateTokenData(Message *);
    bool OnBELDirNotification(Message *);

    // Processes
    Result DoInBottom();
    Result DoOutBottom();
    Result DoInTop();
    Result DoOutTop();

public:
    const Line* FindLine(MemAddr address) const;

    Directory(const std::string& name, ZLCOMA& parent, Clock& clock, size_t numTokens, CacheID firstCache, CacheID lastCache, const Config& config);

    void Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const;
    void Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const;
};

}
#endif
