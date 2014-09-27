#include "Node.h"

#include <iostream>
#include <iomanip>
#include <cstring>
using namespace std;

namespace Simulator
{

// Memory management data
/*static*/ unsigned long                     ZLCDMA::Node::g_References   = 0;
/*static*/ ZLCDMA::Node::Message*            ZLCDMA::Node::g_FreeMessages = NULL;
/*static*/ std::list<ZLCDMA::Node::Message*> ZLCDMA::Node::g_Messages;

/*static*/ void* ZLCDMA::Node::Message::operator new(size_t size)
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

/*static*/ void ZLCDMA::Node::Message::operator delete(void *p, size_t size)
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

/*static*/ void ZLCDMA::Node::PrintMessage(std::ostream& out, const Message& msg)
{
    switch (msg.type)
    {
    case Message::READ:                   out << "| Read Request        "; break;
    case Message::ACQUIRE_TOKENS:         out << "| Acquire Tokens      "; break;
    case Message::EVICTION:               out << "| Eviction            "; break;
    case Message::LOCALDIR_NOTIFICATION:  out << "| Local Dir. Notify   "; break;
    }

    out << " | "
        << "0x"  << hex << setfill('0') << setw(16) << msg.address << " | "
        << right << dec << setfill(' ') << setw( 5) << msg.tokens
        << (msg.priority ? "P" : " ") << " | "
        << setw(6) << msg.source << " | "
        << endl;
}

/*static*/ void ZLCDMA::Node::Print(std::ostream& out, const std::string& name, const Buffer<Message*>& buffer)
{
    std::string sp_left((61 - name.length()) / 2, ' ');
    std::string sp_right(61 - sp_left.length() - name.length(), ' ');

    out <<
    "+-------------------------------------------------------------+\n"
    "|" << sp_left << name << sp_right << "|\n"
    "+----------------------+--------------------+--------+--------+\n"
    "|         Type         |       Address      | Tokens | Sender |\n"
    "+----------------------+--------------------+--------+--------+\n";

    for (Buffer<Message*>::const_iterator p = buffer.begin(); p != buffer.end(); ++p)
    {
        PrintMessage(out, **p);
    }
    out << "+----------------------+--------------------+--------+--------+\n\n";
}

void ZLCDMA::Node::Print(std::ostream& out) const
{
    Print(out, "incoming", m_incoming);
    Print(out, "outgoing", m_outgoing);
}

void ZLCDMA::Node::Initialize(Node* next, Node* prev)
{
    m_prev = prev;
    m_next = next;
    p_Forward.SetStorageTraces(next->m_incoming);
}

Result ZLCDMA::Node::DoForward()
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
bool ZLCDMA::Node::SendMessage(Message* message, size_t min_space)
{
    if (!m_outgoing.Push(message, min_space))
    {
        return false;
    }
    return true;
}

ZLCDMA::Node::Node(const std::string& name, ZLCDMA& parent, Clock& clock)
    : Simulator::Object(name, parent),
      ZLCDMA::Object(name, parent),
      m_prev(NULL),
      m_next(NULL),
      InitStorage(m_incoming, clock, 2),
      InitStorage(m_outgoing, clock, 2),
      InitProcess(p_Forward, DoForward)
{
    g_References++;

    m_outgoing.Sensitive(p_Forward);
}

ZLCDMA::Node::~Node()
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
