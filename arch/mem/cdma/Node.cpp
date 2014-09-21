#include "Node.h"
#include <sim/config.h>

#include <iostream>
#include <iomanip>
#include <cstring>
using namespace std;

namespace Simulator
{

// Memory management data
/*static*/ unsigned long                   CDMA::Node::g_References   = 0;
/*static*/ CDMA::Node::Message*            CDMA::Node::g_FreeMessages = NULL;
/*static*/ std::list<CDMA::Node::Message*> CDMA::Node::g_Messages;

/*static*/ void* CDMA::Node::Message::operator new(size_t size)
{
    // We allocate this many messages at once
    static const size_t ALLOCATE_SIZE = 1024;

    assert(size == sizeof(Message));
    if (g_FreeMessages == NULL)
    {
        // Allocate more messages
        Message* msg = new Message[ALLOCATE_SIZE];
        g_Messages.push_back(msg);

        // Link the new messages into the free list
        for (size_t i = 0; i < ALLOCATE_SIZE; ++i, ++msg)
        {
            msg->next = g_FreeMessages;
            g_FreeMessages = msg;
        }
    }
    assert(g_FreeMessages != NULL);

    // Grab a message off the free list
    Message* msg = g_FreeMessages;
    g_FreeMessages = msg->next;
    return msg;
}

/*static*/ void CDMA::Node::Message::operator delete(void *p, size_t size)
{
    assert(size == sizeof(Message));
    Message* msg = static_cast<Message*>(p);

#ifndef NDEBUG
    // Fill the message with garbage
    memset(msg, 0xFE, sizeof(Message));
#endif

    // Append the message to the free list
    msg->next = g_FreeMessages;
    g_FreeMessages = msg;
}

string CDMA::Node::Message::str() const
{
    ostringstream out;
    switch (type)
    {
    case REQUEST:            out << "[RR "; break;
    case REQUEST_DATA:       out << "[RD "; break;
    case REQUEST_DATA_TOKEN: out << "[RDT"; break;
    case EVICTION:           out << "[EV "; break;
    case UPDATE:             out << "[UP "; break;
    }
    out << " 0x" << hex << address << dec
        << (ignore ? " I+" : " I-")
        << " S" << sender;

    if (type == UPDATE)
        out << " C" << client << ":" << wid;

    switch (type)
    {
    case EVICTION:
    case REQUEST_DATA_TOKEN:
        out << " T" << tokens;
        break;
    default: break;
    }

    switch (type)
    {
    case EVICTION:
    case REQUEST_DATA:
    case REQUEST_DATA_TOKEN:
        out << (dirty ? " D+" : " D-");
        break;
    default: break;
    }
    out << "]@" << this;
    return out.str();
}

/*static*/ void CDMA::Node::Print(std::ostream& out, const std::string& name, const Buffer<Message*>& buffer)
{
    out << "Queue: " << name << ":" << endl;

    for (Buffer<Message*>::const_iterator p = buffer.begin(); p != buffer.end(); ++p)
    {
        out << (**p).str() << endl;
    }
}

void CDMA::Node::Print(std::ostream& out) const
{
    Print(out, "incoming", m_incoming);
    Print(out, "outgoing", m_outgoing);
}

size_t CDMA::Node::GetNumLines() const
{
    return 0;
}

void CDMA::Node::Connect(Node* next, Node* prev)
{
    m_prev = prev;
    m_next = next;
    p_Forward.SetStorageTraces(next->m_incoming);
}

Result CDMA::Node::DoForward()
{
    // Forward requests to the next node
    assert(!m_outgoing.Empty());
    assert(m_next != NULL);

    TraceWrite(m_outgoing.Front()->address, "Sending %s to %s", m_outgoing.Front()->str().c_str(), m_next->GetName().c_str());

    if (!m_next->m_incoming.Push( m_outgoing.Front() ))
    {
        DeadlockWrite("Unable to send request to next node (%s)", m_next->GetName().c_str());
        return FAILED;
    }
    m_outgoing.Pop();
    return SUCCESS;
}

// Send a message to the next node.
// Only succeeds if there's min_space left before the push.
bool CDMA::Node::SendMessage(Message* message, size_t min_space)
{
    if (!m_outgoing.Push(message, min_space))
    {
        return false;
    }
    return true;
}

CDMA::Node::Node(const std::string& name, CDMA& parent, Clock& clock, NodeID id, Config& config)
    : Simulator::Object(name, parent),
      CDMA::Object(name, parent),
      m_id(id),
      m_prev(NULL),
      m_next(NULL),
      m_incoming("b_incoming", *this, clock, config.getValue<BufferSize>(*this, "NodeBufferSize")),
      m_outgoing("b_outgoing", *this, clock, config.getValue<BufferSize>(*this, "NodeBufferSize")),
      p_Forward(*this, "forward", delegate::create<Node, &Node::DoForward>(*this))
{
    g_References++;

    m_outgoing.Sensitive(p_Forward);
}

CDMA::Node::~Node()
{
    assert(g_References > 0);
    if (--g_References == 0)
    {
        // Clean up the allocated messages
        for (std::list<Message*>::const_iterator p = g_Messages.begin(); p != g_Messages.end(); ++p)
        {
            delete[] *p;
        }
        g_Messages.clear();
    }
}

}
