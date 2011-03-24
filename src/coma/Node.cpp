#include "Node.h"
#include "../config.h"
#include <iostream>
#include <iomanip>
#include <cstring>
using namespace std;

namespace Simulator
{

// Memory management data
/*static*/ unsigned long                   COMA::Node::g_References   = 0;
/*static*/ COMA::Node::Message*            COMA::Node::g_FreeMessages = NULL;
/*static*/ std::list<COMA::Node::Message*> COMA::Node::g_Messages;

/*static*/ void* COMA::Node::Message::operator new(size_t size)
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

/*static*/ void COMA::Node::Message::operator delete(void *p, size_t size)
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

/*static*/ void COMA::Node::PrintMessage(std::ostream& out, const Message& msg)
{
    switch (msg.type)
    {
    case Message::REQUEST:            out << "| Read Request        "; break;
    case Message::REQUEST_DATA:       out << "| Read Request (Data) "; break;
    case Message::REQUEST_DATA_TOKEN: out << "| Read Response       "; break;
    case Message::EVICTION:           out << "| Eviction            "; break;
    case Message::UPDATE:             out << "| Update              "; break;
    }
    
    out << " | "
        << "0x" << hex << setfill('0') << setw(16) << msg.address << " | "
        << dec << setfill(' ') << right
        << setw(6);
    
    switch (msg.type)
    {
    case Message::EVICTION:
    case Message::REQUEST_DATA_TOKEN:
        out << msg.tokens;
        break;
        
    case Message::REQUEST:
    case Message::REQUEST_DATA:
    case Message::UPDATE:
        out << ""; 
        break;        
    }
    
    out << " | "
        << setw(6) << msg.sender << " | "
        << endl;
}

void COMA::Node::Print(std::ostream& out) const
{
    const struct {
        const char*             name;
        const Buffer<Message*>& buffer;
    } Buffers[2] = {
        {"incoming", m_incoming},
        {"outgoing", m_outgoing},
    };
    
    for (int i = 0; i < 2; ++i)
    {
        out <<
        "+-------------------------------------------------------------+\n"
        "|                           " << Buffers[i].name << "                          |\n"
        "+----------------------+--------------------+--------+--------+\n"
        "|         Type         |       Address      | Tokens | Sender |\n"
        "+----------------------+--------------------+--------+--------+\n";
        
        const Buffer<Message*>& buffer = Buffers[i].buffer;
        for (Buffer<Message*>::const_iterator p = buffer.begin(); p != buffer.end(); ++p)
        {
            PrintMessage(out, **p);
        }
        out << "+----------------------+--------------------+--------+--------+\n\n";
    }
}

void COMA::Node::Initialize(Node* next, Node* prev)
{
    m_prev = prev;
    m_next = next;
}

Result COMA::Node::DoForward()
{
    // Forward requests to the next node
    assert(!m_outgoing.Empty());
    assert(m_next != NULL);
    
    if (!m_next->m_incoming.Push( m_outgoing.Front() ))
    {
        DeadlockWrite("Unable to send request to next node");
        return FAILED;
    }
    m_outgoing.Pop();
    return SUCCESS;
}

// Send a message to the next node.
// Only succeeds if there's min_space left before the push.
bool COMA::Node::SendMessage(Message* message, size_t min_space)
{
    if (!m_outgoing.Push(message, min_space))
    {
        return false;
    }
    return true;
}

COMA::Node::Node(const std::string& name, COMA& parent, Clock& clock, const Config& config)
    : Simulator::Object(name, parent),
      COMA::Object(name, parent),
      m_prev(NULL),
      m_next(NULL),
      m_incoming("b_incoming", *this, clock, config.getInteger<BufferSize>("COMA_NodeBufferSize", 2)),
      m_outgoing("b_outgoing", *this, clock, config.getInteger<BufferSize>("COMA_NodeBufferSize", 2)),
      p_Forward("forward", delegate::create<Node, &Node::DoForward>(*this))
{
    g_References++;
    
    m_outgoing.Sensitive(p_Forward);
}

COMA::Node::~Node()
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
