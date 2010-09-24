#include "Directory.h"
#include "../../config.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iomanip>
using namespace std;

namespace Simulator
{

// When we shortcut a message over the ring, we want at least one slots
// available in the buffer to avoid deadlocking the ring network. This
// is not necessary for forwarding messages onto the lower ring.
static const size_t MINSPACE_SHORTCUT = 2;
static const size_t MINSPACE_FORWARD  = 1;

ZLCOMA::DirectoryTop::DirectoryTop(const std::string& name, ZLCOMA& parent, Clock& clock)
  : Simulator::Object(name, parent),
    ZLCOMA::Object(name, parent),
    Node(name, parent, clock)
{
}

ZLCOMA::DirectoryBottom::DirectoryBottom(const std::string& name, ZLCOMA& parent, Clock& clock)
  : Simulator::Object(name, parent),
    ZLCOMA::Object(name, parent),
    Node(name, parent, clock)
{
}

// this probably only works with current naive configuration
bool ZLCOMA::Directory::IsBelow(CacheID id) const
{
    return (id >= m_firstCache) && (id <= m_lastCache);
}

ZLCOMA::Directory::Line* ZLCOMA::Directory::FindLine(MemAddr address)
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line* line = &m_lines[set + i];
        if (line->valid && line->tag == tag)
        {
            return line;
        }
    }
    return NULL;
}

const ZLCOMA::Directory::Line* ZLCOMA::Directory::FindLine(MemAddr address) const
{
    const MemAddr tag  = (address / m_lineSize) / m_sets;
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    // Find the line
    for (size_t i = 0; i < m_assoc; ++i)
    {
        const Line* line = &m_lines[set + i];
        if (line->valid && line->tag == tag)
        {
            return line;
        }
    }
    return NULL;
}

ZLCOMA::Directory::Line* ZLCOMA::Directory::GetEmptyLine(MemAddr address)
{
    const size_t  set  = (size_t)((address / m_lineSize) % m_sets) * m_assoc;

    for (size_t i = 0; i < m_assoc; ++i)
    {
        Line* line = &m_lines[set + i];
        if (!line->valid)
        {
            return line;
        }
    }
    return NULL;
}

bool ZLCOMA::Directory::OnMessageReceivedBottom(Message* req)
{
    switch(req->type)
    {
    case Message::ACQUIRE_TOKEN:
        return OnBELAcquireToken(req);
        
    case Message::ACQUIRE_TOKEN_DATA:
        return OnBELAcquireTokenData(req);
        
    case Message::DISSEMINATE_TOKEN_DATA:
        return OnBELDisseminateTokenData(req);
        
    case Message::LOCALDIR_NOTIFICATION:
        return OnBELDirNotification(req);

    default:
        // Error
        abort();
        break;
    }
    return false;
}

bool ZLCOMA::Directory::OnMessageReceivedTop(Message* req)
{
    switch (req->type)
    {
    case Message::ACQUIRE_TOKEN:
        return OnABOAcquireToken(req);
        
    case Message::ACQUIRE_TOKEN_DATA:
        return OnABOAcquireTokenData(req);
        
    case Message::DISSEMINATE_TOKEN_DATA:
        return OnABODisseminateTokenData(req);

    default:
        // Error
        abort();
        break;
    }
    return false;
}

bool ZLCOMA::Directory::OnBELAcquireTokenData(Message* req)
{
    // locate certain set
    Line* line = FindLine(req->address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (IsBelow(req->source))
    {
        if (req->tokenacquired > 0)
        {
            assert(line != NULL || evictedhit);
        }
        
        if (line == NULL)
        {
            // need to fetch a outside the subring

            // Allocate a space, must succeed
            line = GetEmptyLine(req->address);
            assert(line != NULL);

            // update line info
            line->tag = (req->address / m_lineSize) / m_sets;
            line->valid = true;
            line->tokencount = 0;
            line->ntokenline = 0;
            line->ntokenrem = 0;
            line->nrequestin = 0;
            line->nrequestout = 0;
            line->priority = false;

            if (evictedhit)
            {
                // Merge with the evicted line
                unsigned int nrequestin = 0, ntokenrem = 0;
                
                m_evictedlinebuffer.DumpEvictedLine2Line(req->address, nrequestin, ntokenrem);
                
                line->nrequestin += nrequestin;
                line->ntokenrem  += ntokenrem;
            }

            // save the request
            line->nrequestout++;
            
            if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
            return true;
        }

        // make sure that no buffer hit
        assert(evictedhit == false);

        if (req->tokenrequested < m_numTokens)  // read: RS, SR
        {
            assert(req->transient == false);
            
            if (line->ntokenline + line->ntokenrem <= 0 || req->processed)
            {
                // no token in local level, the line must be acquiring tokens from somewhere else

                // transfer tokens to request, if any.
                line->nrequestout++;
                
                line->ntokenline   -= req->tokenacquired;
                
                req->tokenacquired += line->tokencount;
                req->priority       = req->priority || line->priority;

                line->tokencount  = 0;
                line->priority    = false;

                // REVISIT
                if (line->ntokenrem < 0)
                {
                    assert(line->ntokenline > 0);
                    line->ntokenline += line->ntokenrem;
                    line->ntokenrem = 0;
                }

                // means that some previous DD has been absorbed by lines,
                // current states are not so precise, thus reorganize
                if (line->ntokenline < 0)
                {
                    assert(line->ntokenrem > 0);
                    line->ntokenrem += line->ntokenline;
                    line->ntokenline = 0;
                }
                assert(line->ntokenline >= 0);

                // Do not care when remote request come in, mind only the cases that local to local or local to global
                if (line->ntokenline == 0 && line->nrequestout == 0)
                {
                    // Evict line
                    assert(line->ntokenrem >= 0);
                    assert(line->nrequestout == 0);

                    if (line->ntokenrem > 0 || line->nrequestin > 0)
                    {
                        // Need to check out and deal with it, REVISIT
                        // Put the information to the evicted line buffer.
                        assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                        m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                    }

                    line->valid = false;
                }

                // send the request to upper level
                if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
                {
                    return false;
                }
            }
            else
            {
                // If there are lines in the same level then let the request stay in the same level
                
                // if directory has tokens, hand them over to the reqeust.
                if (line->tokencount > 0)
                {
                    line->ntokenline   += line->tokencount;

                    req->tokenacquired += line->tokencount;
                    req->priority      = req->priority || line->priority;

                    line->tokencount    = 0;
                    line->priority      = false;

                    // REVISIT
                    if (line->ntokenrem < 0)
                    {
                        assert(line->ntokenline > 0);
                        line->ntokenline += line->ntokenrem;
                        line->ntokenrem = 0;
                    }

                    // means that some previous DD has been absorbed by lines,
                    // current states are not so precise, thus reorganize
                    if (line->ntokenline < 0)
                    {
                        assert(line->ntokenrem > 0);

                        line->ntokenrem += line->ntokenline;
                        line->ntokenline = 0;
                    }
                    assert(line->ntokenline >= 0);

                    if (line->ntokenline == 0 && line->nrequestout == 0)
                    {
                        // Evict line
                        assert(line->ntokenrem >= 0);
                        assert(line->nrequestout == 0);

                        if (line->ntokenrem > 0 || line->nrequestin > 0)
                        {
                            // need to check out and deal with it, REVISIT
                            // Put the information in the evicted line buffer.
                            assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                            m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                        }

                        line->valid = false;
                    }
                }

                // save the reqeust 
                if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
                {
                    return false;
                }
            }
        }
        // RE, ER
        else if (line->tokencount + line->ntokenline + line->ntokenrem != m_numTokens || line->nrequestin != 0 || line->nrequestout != 0)
        {
            // need to go out the local level

            // Update request and line
            line->nrequestout++;

            if (req->transient && line->priority)
            {
                req->transient = false;
                req->priority  = true;
                line->priority  = false;
            }
            
            int newlinetokenline = line->ntokenline - (req->transient ? 0 : req->tokenacquired);

            req->tokenacquired += line->tokencount;
            req->priority      = req->priority || line->priority;

            line->tokencount    = 0;
            line->ntokenline    = newlinetokenline;
            line->priority      = false;

            // REVISIT
            if (line->ntokenrem < 0)
            {
                assert(line->ntokenline > 0);
                line->ntokenline += line->ntokenrem;
                line->ntokenrem = 0;
            }

            // means that some previous DD has been absorbed by lines,
            // current states are not so precise, thus reorganize
            if (line->ntokenline < 0)
            {
                assert(line->ntokenrem > 0);

                line->ntokenrem += line->ntokenline;
                line->ntokenline = 0;
            }
            assert(line->ntokenline >= 0);

            if (line->ntokenline == 0 && line->nrequestout == 0)
            {
                // Evict line
                assert(line->ntokenrem >= 0);
                assert(line->nrequestout == 0);
                
                if (line->ntokenrem > 0 || line->nrequestin > 0)
                {
                    // need to check out and deal with it, REVISIT

                    // Put the information in the evicted line buffer
                    assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                    m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                }

                line->valid = false;
            }

            if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
        }
        else
        {
            // All tokens are in local level; not necessary to go outside.
            
            // Make sure at least some cache has the data.
            assert(line->ntokenline + line->ntokenrem > 0);

            req->tokenacquired += line->tokencount;
            req->priority      = req->priority || line->priority;
            
            if (!req->transient)
            {
                line->ntokenline += line->tokencount;
                line->tokencount  = 0;
            }
            line->priority = false;

            // REVISIT
            if (line->ntokenrem < 0)
            {
                assert(line->ntokenline > 0);
                line->ntokenline += line->ntokenrem;
                line->ntokenrem = 0;
            }

            // means that some previous DD has been absorbed by lines,
            // current states are not so precise, thus reorganize
            if (line->ntokenline < 0)
            {
                assert(line->ntokenrem > 0);

                line->ntokenrem += line->ntokenline;
                line->ntokenline = 0;
            }
            assert(line->ntokenline >= 0);

            if (line->ntokenline == 0 && line->nrequestout == 0)
            {
                // Evict line
                assert(line->ntokenrem >= 0);
                assert(line->nrequestout == 0);
                
                if (line->ntokenrem > 0 || line->nrequestin > 0)
                {
                    // need to check out and deal with it, REVISIT

                    // Put the information in the evicted line buffer
                    assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                    m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                }

                line->valid = false;
            }

            if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
        }
    }
    else if (line == NULL)
    {
        // Probably there should be remote request inside local level in this case
        assert(evictedhit);
        m_evictedlinebuffer.UpdateEvictedLine(req->address, false, req->gettokenpermanent());

        // Send the request to the upper level
        if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    else
    {
        assert(evictedhit == false);

        // get token from the directory if any        
        line->nrequestin--;

        // Send the request to upper level
        if (!req->transient)
        {
            int newlinetokenline = line->ntokenline - req->tokenacquired;

            req->tokenacquired += line->tokencount;
            req->priority      = req->priority || line->priority;

            line->tokencount = 0;
            line->ntokenline = newlinetokenline;
            line->priority = false;
        }

        // REVISIT
        if (line->ntokenrem < 0)
        {
            assert(line->ntokenline > 0);
            line->ntokenline += line->ntokenrem;
            line->ntokenrem = 0;
        }

        // means that some previous DD has been absorbed by lines,
        // current states are not so precise, thus reorganize
        if (line->ntokenline < 0)
        {
            assert(line->ntokenrem > 0);

            line->ntokenrem += line->ntokenline;
            line->ntokenline = 0;
        }
        assert(line->ntokenline >= 0);

        if (line->ntokenline == 0 && line->nrequestout == 0)
        {
            // Evict line
            assert(line->ntokenrem >= 0);
            assert(line->nrequestout == 0);
            
            if (line->ntokenrem > 0 || line->nrequestin > 0)
            {
                assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
            }

            line->valid = false;
        }

        // remote request is going out anyway
        if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    return true;
}


bool ZLCOMA::Directory::OnBELAcquireToken(Message* req)
{
    assert(req->tokenrequested == m_numTokens);

    Line* line = FindLine(req->address);

    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (IsBelow(req->source))
    {
        if (line == NULL)
        {
            assert(req->tokenacquired == 0);
            // need to fetch a outside the subring

            // Allocate a space, must succeed
            line = GetEmptyLine(req->address);
            assert(line != NULL);

            // update line info
            line->tag = (req->address / m_lineSize) / m_sets;
            line->valid = true;
            line->tokencount = 0;
            line->ntokenline = 0;
            line->ntokenrem = 0;
            line->nrequestin = 0;
            line->nrequestout = 0;
            line->priority = false;

            if (evictedhit)
            {
                unsigned int nrequestin = 0, ntokenrem = 0;
                
                m_evictedlinebuffer.DumpEvictedLine2Line(req->address, nrequestin, ntokenrem);
                
                line->nrequestin += nrequestin;
                line->ntokenrem += ntokenrem;
            }

            // save the request
            if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }

            line->nrequestout++;
        }
        else
        {
            assert(evictedhit == false);
            assert(req->tokenacquired > 0);

            // request is IV
            if (line->tokencount + line->ntokenline + line->ntokenrem != m_numTokens || line->nrequestin != 0 || line->nrequestout != 0)
            {
                // need to go out the local level

                // Update request and line
                line->nrequestout++;
                if (req->transient && line->priority)
                {
                    req->transient = false;
                    req->priority  = true;
                    line->priority  = false;
                }
                int newlinetokenline = line->ntokenline - req->gettokenpermanent();

                req->tokenacquired += line->tokencount;
                req->priority      = req->priority || line->priority;

                line->tokencount    = 0;
                line->ntokenline    = newlinetokenline;
                line->priority      = false;

                // REVISIT
                if (line->ntokenrem < 0)
                {
                    assert(line->ntokenline > 0);
                    line->ntokenline += line->ntokenrem;
                    line->ntokenrem = 0;
                }

                // means that some previous DD has been absorbed by lines,
                // current states are not so precise, thus reorganize
                if (line->ntokenline < 0)
                {
                    assert(line->ntokenrem > 0);

                    line->ntokenrem += line->ntokenline;
                    line->ntokenline = 0;
                }
                assert(line->ntokenline >= 0);

                if (line->ntokenline == 0 && line->nrequestout == 0)
                {
                    if (line->ntokenrem > 0 || line->nrequestin > 0)
                    {
                        // need to check out and deal with it, REVISIT
                        assert(line->nrequestout == 0);

                        // evict the line, and put the info in evicted line buffer in advance
                        // add the information to the evicted line buffer
                        bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);
                        assert(evictedhit == false);
                        m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                    }
                    else
                    {
                        assert(line->ntokenrem >= 0);
                        assert(line->nrequestout == 0);
                    }

                    // evict line
                    line->valid = false;
                }

                if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
                {
                    return false;
                }
            }
            else // all tokens are in local level
            {
                //no necessary to go outside

                // make sure at least some cache has the data
                assert(line->ntokenline + line->ntokenrem > 0);

                // Update request and line
                req->tokenacquired += line->tokencount;
                req->priority      = req->priority || line->priority;
                    
                if (!req->transient)
                {
                    line->ntokenline += line->tokencount;                    
                    line->tokencount  = 0;
                }
                line->priority = false;

                // REVISIT
                if (line->ntokenrem < 0)
                {
                    assert(line->ntokenline > 0);
                    line->ntokenline += line->ntokenrem;
                    line->ntokenrem = 0;
                }

                // means that some previous DD has been absorbed by lines,
                // current states are not so precise, thus reorganize
                if (line->ntokenline < 0)
                {
                    assert(line->ntokenrem > 0);

                    line->ntokenrem += line->ntokenline;
                    line->ntokenline = 0;
                }
                assert(line->ntokenline >= 0);

                if (line->ntokenline == 0 && line->nrequestout == 0)
                {
                    // Evict line
                    assert(line->ntokenrem >= 0);
                    assert(line->nrequestout == 0);
                    
                    if (line->ntokenrem > 0 || line->nrequestin > 0)
                    {
                        // need to check out and deal with it, REVISIT

                        // Put the information in the evicted line buffer
                        assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                        m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                    }

                    line->valid = false;
                }

                if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
                {
                    return false;
                }
            }
        }
    }
    else
    {
        if (line == NULL)
        {
            // prepare the request to send to upper level

            // just go out
            if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }

            assert (evictedhit);
	        m_evictedlinebuffer.UpdateEvictedLine(req->address, false, req->gettokenpermanent());
        }
        else
        {
            assert(evictedhit == false);

            // get token from the directory if any        
            line->nrequestin--;

            // Send the request to upper level
            if (!req->transient)
            {
                int newlinetokenline = line->ntokenline - req->tokenacquired;

                req->tokenacquired += line->tokencount;
                req->priority      = req->priority || line->priority;

                line->tokencount = 0;
                line->ntokenline = newlinetokenline;
                line->priority = false;
            }

            // REVISIT
            if (line->ntokenrem < 0)
            {
                assert(line->ntokenline > 0);
                line->ntokenline += line->ntokenrem;
                line->ntokenrem = 0;
            }

            // means that some previous DD has been absorbed by lines,
            // current states are not so precise, thus reorganize
            if (line->ntokenline < 0)
            {
                assert(line->ntokenrem > 0);

                line->ntokenrem += line->ntokenline;
                line->ntokenline = 0;
            }
            assert(line->ntokenline >= 0);

            if (line->ntokenline == 0 && line->nrequestout == 0)
            {
                // Evict line
                assert(line->ntokenrem >= 0);
                assert(line->nrequestout == 0);
                
                if (line->ntokenrem > 0 || line->nrequestin > 0)
                {
                    // need to check out and deal with it, REVISIT
                    assert(m_evictedlinebuffer.FindEvictedLine(req->address) == false);
                    m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
                }

                line->valid = false;
            }

            // remote request is going out anyway
            if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
        }
    }
    return true;
}

bool ZLCOMA::Directory::OnBELDisseminateTokenData(Message* req)
{
    assert(req->tokenacquired > 0);     // Should have tokens
    assert(req->transient == false);    // EV/WB can never have transient tokens

    // EV request will always terminate at directory

    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    Line* line = FindLine(req->address);
    if (line == NULL)
    {
        // We don't have the line, it must have been evicted.
        assert(evictedhit);
        m_evictedlinebuffer.UpdateEvictedLine(req->address, false, req->tokenacquired, true);
        if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
        return true;
    }
    assert(!evictedhit);

    if (IsBelow(req->source))
    {
        // issue: the eviction request might go around the local network, in turn change the request sequence.
        // this might generate leave the root directory without sufficient space
        // solution: stack the request tokens on the directory line 
        // if there are request in or out, then go global, since stacking the token might lost data
        // if nothing in or out, and there are tokens locally, then stack them on the line
        // if all the tokens are stacked on the line, then request will be delievered to outside 
        // otherwise deleted
        // NOT IMPLEMENTED YET
        //
        // Possible issue: sacrifice locality. for example, if locally another line is request, while all the
        // tokens are evicted to outside. without evicted to the same level and informing the request line,
        // locality might be suffered. JXXX, REVISIT
        //
        // possibly, deal with rquest out differently as well, REVISIT JXXX
        if (line->nrequestin == 0 && line->nrequestout == 0 && (int)req->tokenacquired < line->ntokenline)
        {
            if (req->tokenrequested == 0)   // EV
            {
                // just stack, no ripping
                line->ntokenline -= req->tokenacquired;
                line->tokencount += req->tokenacquired;
                line->priority    = line->priority || req->priority;
                delete req;
                return true;
            }

            // WB
            assert(req->tokenrequested == m_numTokens);
        }
    }
    
    line->ntokenline   -= req->tokenacquired;
    req->tokenacquired += line->tokencount;
    req->priority       = req->priority || line->priority;
    line->tokencount    = 0;
    line->priority      = false;
 
    if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
    {
        return false;
    }

    // REVISIT
    if (line->ntokenrem < 0)
    {
        assert(line->ntokenline > 0);
        line->ntokenline += line->ntokenrem;
        line->ntokenrem = 0;
    }

    // means that some previous DD has been absorbed by lines,
    // current states are not so precise, thus reorganize
    if (line->ntokenline < 0)
    {
        assert(line->ntokenrem > 0);

        line->ntokenrem += line->ntokenline;
        line->ntokenline = 0;
    }
    assert(line->ntokenline >= 0);

    if (line->ntokenline == 0 && line->nrequestout == 0)
    {
        // Evict line
        assert(line->ntokenrem >= 0);
        assert(line->nrequestout == 0);
        
        if (line->ntokenrem > 0 || line->nrequestin > 0)
        {
            // need to check out and deal with it, REVISIT
            m_evictedlinebuffer.AddEvictedLine(req->address, line->nrequestin, line->ntokenrem);
        }

        line->valid = false;
    }
    return true;
}

bool ZLCOMA::Directory::OnBELDirNotification(Message* req)
{
    // We should have this line
    Line* line = FindLine(req->address);
    assert(line != NULL);
    
    // Add the tokens
    line->ntokenline += req->tokenacquired;

    // Terminate the request
    delete req;
    return true;
}

bool ZLCOMA::Directory::OnABOAcquireTokenData(Message* req)
{
    Line* line = FindLine(req->address);

    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (IsBelow(req->source))
    {
        assert (line != NULL);

        // Update the dir
        line->nrequestout--;

        // Send the request to local
        if (!req->transient)
        {
            int newlinetokenline = line->ntokenline + req->tokenacquired;

            req->tokenacquired += line->tokencount;
            req->priority      = req->priority || line->priority;

            line->ntokenline    = newlinetokenline;
        }
        else
        {
            if (line->priority)
            {
                req->transient = false;
                req->priority  = true;
                line->priority  = false;
                
                line->ntokenline += req->tokenacquired;
            }

            req->tokenacquired += line->tokencount;
            req->priority       = req->priority || line->priority;
        }

        line->priority = false;
        line->tokencount = 0;

        // REVISIT
        if (line->ntokenrem < 0)
        {
            assert(line->ntokenline > 0);
            line->ntokenline += line->ntokenrem;
            line->ntokenrem = 0;
        }

        // means that some previous DD has been absorbed by lines,
        // current states are not so precise, thus reorganize
        if (line->ntokenline < 0)
        {
            assert(line->ntokenrem > 0);

            line->ntokenrem += line->ntokenline;
            line->ntokenline = 0;
        }
        assert(line->ntokenline >= 0);

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }

        // always go local
        if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    // remote request
    // somehting inside lower level, just always get in
    else if (evictedhit)
    {
        // get in lower level, but update the evicted buffer
        m_evictedlinebuffer.UpdateEvictedLine(req->address, true, req->gettokenpermanent());

        // get in lower level
        if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    // as long as the line exist, the requet, no matter RS or RE, has to get in
    else if (line == NULL)
    {
        // This line is not below this directory; forward request onto upper ring
        if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    else
    {
        line->nrequestin++;

        if (req->transient && line->priority)
        {
            line->priority = false;
            req->transient = false;
            req->priority = true;
        }

        if (!req->transient)
        {
            req->tokenacquired += line->tokencount;
            line->tokencount    = 0;
            line->ntokenrem    += req->tokenacquired;
        }

        req->priority = req->priority || line->priority;
        line->priority = false;

        // REVISIT
        if (line->ntokenrem < 0)
        {
            assert(line->ntokenline > 0);
            line->ntokenline += line->ntokenrem;
            line->ntokenrem = 0;
        }

        // means that some previous DD has been absorbed by lines,
        // current states are not so precise, thus reorganize
        if (line->ntokenline < 0)
        {
            assert(line->ntokenrem > 0);

            line->ntokenrem += line->ntokenline;
            line->ntokenline = 0;
        }
        assert(line->ntokenline >= 0);

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }

        // get in lower level
        if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    return true;
}

bool ZLCOMA::Directory::OnABOAcquireToken(Message* req)
{
    Line* line = FindLine(req->address);

    // evicted line buffer
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address);

    if (IsBelow(req->source))
    {
        assert(line != NULL);

        // Update the dir
        line->nrequestout--;

        // Send the request to local
        if (req->transient)
        {
            if (line->priority) {
                req->transient = false;
                req->priority  = true;
                line->priority = false;
            } else {
                line->ntokenline += req->tokenacquired;
            }
        }

        req->priority       = req->priority || line->priority;        
        req->tokenacquired += line->tokencount;

        line->priority   = false;
        line->tokencount = 0;

        // REVISIT
        if (line->ntokenrem < 0)
        {
            assert(line->ntokenline > 0);
            line->ntokenline += line->ntokenrem;
            line->ntokenrem = 0;
        }

        // means that some previous DD has been absorbed by lines,
        // current states are not so precise, thus reorganize
        if (line->ntokenline < 0)
        {
            assert(line->ntokenrem > 0);

            line->ntokenrem += line->ntokenline;
            line->ntokenline = 0;
        }
        assert(line->ntokenline >= 0);

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }

        // always go local
        if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    // remote request
    else if (evictedhit)
    {
        // Update the evicted buffer
        m_evictedlinebuffer.UpdateEvictedLine(req->address, true, req->gettokenpermanent());

        // Put request on lower ring
        if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    else if (line == NULL)
    {
        // The line does not exist below this directory; forward message on upper ring
        if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    else    // somehting inside lower level, just always get in
    {
        line->nrequestin++;

        if (req->transient && line->priority)
        {
            line->priority = false;
            req->transient = false;
            req->priority = true;
        }

        if (!req->transient)
        {
            req->tokenacquired += line->tokencount;
            line->ntokenrem    += req->tokenacquired;
            line->tokencount    = 0;
        }
        
        req->priority = req->priority || line->priority;

        line->priority = false;

        // REVISIT
        if (line->ntokenrem < 0)
        {
            assert(line->ntokenline > 0);
            line->ntokenline += line->ntokenrem;
            line->ntokenrem = 0;
        }

        // means that some previous DD has been absorbed by lines,
        // current states are not so precise, thus reorganize
        if (line->ntokenline < 0)
        {
            assert(line->ntokenrem > 0);

            line->ntokenrem += line->ntokenline;
            line->ntokenline = 0;
        }
        assert(line->ntokenline >= 0);

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }

        // Put message on lower ring
        if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    return true;
}

bool ZLCOMA::Directory::OnABODisseminateTokenData(Message* req)
{
    // EV request will always terminate at directory
    assert(req->tokenacquired > 0);
    assert(req->transient == false);

    Line* line = FindLine(req->address);

    // evicted line buffer
    unsigned int requestin = 0;
    unsigned int tokenrem;
    bool evictedhit = m_evictedlinebuffer.FindEvictedLine(req->address, requestin, tokenrem);

    // does not matter whether the request is from local level or not

    // issue: disseminated token if send to lower level, 
    // the replaced request from the evicted line can bypass the evicted token, 
    // which lead to insufficient lines in root directory
    // solution & analysis:
    // if tokens available in the lowerlevel, and they are not evicted to upper level yet, it's safe
    // if tokens not avaialbe, then skip the group and move to next group
    // if tokens are avaialbe, there are request in or out, then go in, it's safe
    // if tokens are avaible in evicted buffer, if there are request in, then get in, otherwise, skip to next group
    // if tokens are available in the line, there are no request in or out, then the line can be there or evicted. and lines should be or ever be in normal state. thus, leave the tokens and priority flag and other stuff directly in the directory is fine. in this case, the request should be terminated here.

    if (evictedhit) // REVIST, JXXX, this may not be necessary
    {
        if (requestin == 0)
        {
            // skip the local group to next group
            if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
        }
        else
        {
            // get in lower level, but update the evicted buffer
            m_evictedlinebuffer.UpdateEvictedLine(req->address, true, req->tokenacquired, true);

            // lower level
            if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
            {
                return false;
            }
        }
    }
    else if (line == NULL)
    {
        // skip the local level and pass it on
        if (!DirectoryTop::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }
    }
    else if (line->nrequestin != 0 || line->nrequestout != 0)
    {
        // lower level
        if (!DirectoryBottom::SendMessage(req, MINSPACE_FORWARD))
        {
            return false;
        }

        req->tokenacquired += line->tokencount;
        line->tokencount    = 0;
        line->ntokenrem    += req->tokenacquired;
        req->priority       = req->priority || line->priority;
        line->priority      = false;

        // REVISIT
        if (line->ntokenrem < 0)
        {
            assert(line->ntokenline > 0);
            line->ntokenline += line->ntokenrem;
            line->ntokenrem = 0;
        }

        // means that some previous DD has been absorbed by lines,
        // current states are not so precise, thus reorganize
        if (line->ntokenline < 0)
        {
            assert(line->ntokenrem > 0);

            line->ntokenrem += line->ntokenline;
            line->ntokenline = 0;
        }
        assert(line->ntokenline >= 0);

        if (line->nrequestin == 0 && line->nrequestout == 0 && line->tokencount == 0 && line->ntokenline  == 0 && line->ntokenrem == 0)
        {
            line->valid = false;
        }
    }
    else
    {
        assert(line->ntokenline + line->ntokenrem > 0);
        // leave the tokens on the line. without getting in or send to the next node

        // notgoing anywhere, just terminate the request
        assert(req->tokenacquired < m_numTokens);
        line->tokencount += req->tokenacquired;
        line->priority = line->priority || req->priority;

        delete req;
    }
    return true;
}

Result ZLCOMA::Directory::DoInBottom()
{
    // Handle incoming message on bottom ring from previous node
    assert(!DirectoryBottom::m_incoming.Empty());
    if (!OnMessageReceivedBottom(DirectoryBottom::m_incoming.Front()))
    {
        return FAILED;
    }
    DirectoryBottom::m_incoming.Pop();
    return SUCCESS;
}

Result ZLCOMA::Directory::DoInTop()
{
    // Handle incoming message on top ring from previous node
    assert(!DirectoryTop::m_incoming.Empty());
    if (!OnMessageReceivedTop(DirectoryTop::m_incoming.Front()))
    {
        return FAILED;
    }
    DirectoryTop::m_incoming.Pop();
    return SUCCESS;
}

ZLCOMA::Directory::Directory(const std::string& name, ZLCOMA& parent, Clock& clock, size_t numTokens, CacheID firstCache, CacheID lastCache, const Config& config) :
    Simulator::Object(name, parent),
    ZLCOMA::Object(name, parent),
    DirectoryBottom(name + "-bottom", parent, clock),
    DirectoryTop(name + "-top", parent, clock),
    p_lines     (*this, clock, "p_lines"),
    m_lineSize  (config.getInteger<size_t>("CacheLineSize",           64)),
    m_assoc     (config.getInteger<size_t>("COMACacheAssociativity",   4) * (lastCache - firstCache + 1)),
    m_sets      (config.getInteger<size_t>("COMACacheNumSets",       128)),
    m_numTokens (numTokens),
    m_firstCache(firstCache),
    m_lastCache (lastCache),
    p_InBottom  ("bottom-incoming", delegate::create<Directory, &Directory::DoInBottom >(*this)),
    p_InTop     ("top-incoming",    delegate::create<Directory, &Directory::DoInTop    >(*this))
{
    // Create the cache lines
    // We need as many cache lines in a directory to cover all caches below it
    m_lines.resize(m_assoc * m_sets);
    for (size_t i = 0; i < m_lines.size(); ++i)
    {
        Line& line = m_lines[i];
        line.valid = false;
    }

    DirectoryBottom::m_incoming.Sensitive(p_InBottom);
    DirectoryTop   ::m_incoming.Sensitive(p_InTop);

    p_lines.AddProcess(p_InTop);
    p_lines.AddProcess(p_InBottom);
}

void ZLCOMA::Directory::Cmd_Help(std::ostream& out, const std::vector<std::string>& arguments) const
{
    out <<
    "The Directory in a COMA system is connected via other nodes in the COMA\n"
    "system via a ring network.\n\n"
    "Supported operations:\n"
    "- read <component>\n"
    "  Reads and displays the directory lines, and global information such as hit-rate\n"
    "  and directory configuration.\n"
    "- read <component> buffers\n"
    "  Reads and displays the buffers in the directory\n";
}

void ZLCOMA::Directory::Cmd_Read(std::ostream& out, const std::vector<std::string>& arguments) const
{
    if (!arguments.empty() && arguments[0] == "buffers")
    {
        // Read the buffers
        out << endl << "Top ring interface:" << endl << endl;
        DirectoryTop::Print(out);

        out << endl << "Bottom ring interface:" << endl << endl;
        DirectoryBottom::Print(out);

        return;
    }

    out << "Cache type:  ";
    if (m_assoc == 1) {
        out << "Direct mapped" << endl;
    } else if (m_assoc == m_lines.size()) {
        out << "Fully associative" << endl;
    } else {
        out << dec << m_assoc << "-way set associative" << endl;
    }
    out << "Cache range: " << m_firstCache << " - " << m_lastCache << endl;
    out << endl;

    // No more than 4 columns per row and at most 1 set per row
    const size_t width = std::min<size_t>(m_assoc, 4);

    out << "Set |";
    for (size_t i = 0; i < width; ++i) out << "       Address      | Tokens |";
    out << endl << "----";
    std::string seperator = "+";
    for (size_t i = 0; i < width; ++i) seperator += "--------------------+--------+";
    out << seperator << endl;

    for (size_t i = 0; i < m_lines.size() / width; ++i)
    {
        const size_t index = (i * width);
        const size_t set   = index / m_assoc;

        if (index % m_assoc == 0) {
            out << setw(3) << setfill(' ') << dec << right << set;
        } else {
            out << "   ";
        }

        out << " | ";
        for (size_t j = 0; j < width; ++j)
        {
            const Line& line = m_lines[index + j];
            if (line.valid) {
                out << hex << "0x" << setw(16) << setfill('0') << (line.tag * m_sets + set) * m_lineSize << " | "
                    << dec << setfill(' ') << setw(6) << line.tokencount;
            } else {
                out << "                   |       ";
            }
            out << " | ";
        }
        out << endl
            << ((index + width) % m_assoc == 0 ? "----" : "    ")
            << seperator << endl;
    }
}

}
