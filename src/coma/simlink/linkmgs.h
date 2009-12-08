#ifndef LINKMIKE_H
#define LINKMIKE_H
#include "../simlink/memorydatacontainer.h"
#include "../memorys/dcswitch.h"
#include <cstdlib>
#include <stdint.h>

#include <vector>
#include <iostream>

class LinkConfig
{
public:
    unsigned int m_nLineSize;
    unsigned int m_nProcLink;
    unsigned int m_nProcMGS;
    unsigned int m_nCache;
    unsigned int m_nDirectory;
    unsigned int m_nSplitRootNumber;
    unsigned int m_nMemoryChannelNumber;
    unsigned int m_nChannelInterleavingScheme;
    uint64_t m_nStartingAddress;
    uint64_t m_nMemorySize;
    unsigned int m_nCacheSet;
    unsigned int m_nCacheAssociativity;
    bool         m_bEager;
    unsigned int m_nCacheAccessTime;
    unsigned int m_nMemoryAccessTime;
    unsigned int m_nDefaultVerbose;
    const char*  m_sDDRXML;
    unsigned int m_nDDRConfigID;
    unsigned int m_nInject;

    unsigned int m_nCycleTimeCore;
    unsigned int m_nCycleTimeMemory;

    //char * m_pMemoryMapFile;
    char * m_pGlobalLogFile;

    bool m_bConfigDone;
    
LinkConfig() : m_bConfigDone(false) {
        m_nProcLink = 1;

        m_nStartingAddress = 0;
        m_nMemorySize = 0x2000000;

        m_bEager = false;

        m_nMemoryAccessTime = 100;
        m_nDefaultVerbose = 3;

        m_sDDRXML = DDRXML_CONFIG_PATH;

        //m_pMemoryMapFile = NULL;
	m_pGlobalLogFile = NULL;
    }
    void dumpConfiguration(std::ostream& os) const
    {
#define PC(N) "# " #N << '\t' << m_ ## N << std::endl 
        os << "### begin COMA configuration" << std::endl
           << PC(nProcMGS)
           << PC(nProcLink)
           << PC(nCache)
           << PC(nDirectory)
           << PC(nSplitRootNumber)
           << PC(nMemoryChannelNumber)
           << PC(nChannelInterleavingScheme)
           << PC(sDDRXML)
           << PC(nDDRConfigID)
           << PC(nCycleTimeCore)
           << PC(nCycleTimeMemory)
           << PC(nCacheAccessTime)
           << PC(nMemoryAccessTime)
           << PC(nCacheSet)
           << PC(nCacheAssociativity)
           << PC(nLineSize)
           << PC(nInject)
           << PC(bEager)
           << "### end COMA configuration" << std::endl;
#undef PC
    };
};

class LinkMGS
{
public:
    // link flag
    enum LINKFLAG{
        LF_NONE,	    // no request linked
        LF_AVAILABLE,	// a linked request is available
        LF_PROCESSED	// a linked request is processed
    };

    static LinkConfig s_oLinkConfig;
protected:
    // link flags
    LINKFLAG m_eRequestIni;
    LINKFLAG m_eRequestDone;

public:
    LinkMGS(){
        m_eRequestIni = LF_NONE;
        m_eRequestDone = LF_NONE;
    }

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
    std::vector<LinkMGS*>    m_vecLinkPeers;
#endif

    virtual ~LinkMGS(){}

    // initiate a request to systemc
    virtual void PutRequest(uint64_t address, bool write, uint64_t size, void* data, unsigned long* ref) = 0;
    //virtual void PutRequest(unsigned int address, bool write, unsigned int size, void* data, unsigned long* ref){PutRequest((uint64_t)address, write, (unsigned int)size, data, ref);};

    // check and get a reply from the systemc
    // NULL means no reply 
    // returned address value can be found in 
    // the request structure pointed by the replied pointer as well
    // extcode: extension code for reply
    //  0 : normal read/write reply request
    //  1 : backward broadcast invalidation request
    // for IB request, address is used. the value is LINE ADDRESS, with asscociated L2 block size +++
    virtual unsigned long* GetReply(uint64_t &address, void* data, uint64_t &size, int &extcode) = 0;
    virtual unsigned long* GetReply(uint32_t &address, void* data, uint64_t &size, int &extcode){return GetReply((uint64_t&)address, data, size, extcode);}

    virtual bool RemoveReply() = 0;

#ifdef MEM_CACHE_LEVEL_ONE_SNOOP
    void BindPeers(std::vector<LinkMGS*> vecpeers){m_vecLinkPeers = vecpeers;}
/*    
// snoop read will try local first then try L2 
virtual bool OnMemorySnoopRead(uint64_t address, void *data, unsigned int size, bool dcache) = 0;
// snoop write will try both directly
virtual bool OnMemorySnoopWrite(uint64_t address, void *data, unsigned int size) = 0;
// snoop ib is from the cache side request
virtual bool OnMemorySnoopIB(uint64_t address, void *data, unsigned int size) = 0;
*/
    // Set m_pCMLink pointer
    void SetCMLinkPTR(void* cmlink){m_pCMLink = cmlink;}

    // get m_pCMLink pointer
    void* GetCMLinkPTR(){return m_pCMLink;}

protected:
    void * m_pCMLink;
#endif

};

extern MemoryDataContainer* g_pMemoryDataContainer;
extern LinkMGS** g_pLinks;

extern std::ofstream g_osMonitorFile;
extern uint64_t g_u64MonitorAddress;

#include "memstat.h"

// some command functions can be called
void setverboselevel(int nlevel);

// dump the caches and memory into a file
void dumpcacheandmemory(std::ofstream& filestream, bool bforce);

// gather line data for debug
unsigned int gatherlinevalue(uint64_t addr, char* linedata);

// check the cache and memory consistency
// return : false if the checking process failed
// return :  true if the checking process succeed
// whether the caches and memory are consistent or not 
// is not given by the boolean return value
bool checkcacheandmemory();

void reviewmemorysystem();

#ifdef MEM_MODULE_STATISTICS
void printstatistics(const char* filename);
#endif

void startmonitorfile(const char* filename);
void stopmonitorfile();
void monitormemoryaddress(uint64_t address);
void automonitoraddress(uint64_t address);
#endif

