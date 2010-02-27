#include "Node.h"
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

COMA::Node::Interface::Interface(const Object& object, const std::string& name)
    : incoming(*object.GetKernel(), 2),
      outgoing(*object.GetKernel(), 3, 2),
      arbitrator(object, name + ".arbitrator")
{
}

bool COMA::Node::Interface::Send(Message* message)
{
    if (!arbitrator.Invoke())
    {
        return false;
    }
    
    if (!outgoing.Push(message))
    {
        return false;
    }
    
    return true;
}

/*static*/ void COMA::Node::Interface::PrintMessage(std::ostream& out, const Message& msg)
{
    switch (msg.type)
    {
    case Message::REQUEST_READ:         out << "| Request: Read        "; break;
    case Message::REQUEST_EVICT:        out << "| Request: Evict       "; break;
    case Message::REQUEST_KILL_TOKENS:  out << "| Request: Kill Tokens "; break;
    case Message::REQUEST_UPDATE:       out << "| Request: Update      "; break;
    case Message::RESPONSE_READ:        out << "| Response: Read       "; break;
    case Message::RESPONSE_FORWARD:     out << "| Response: Forward    "; break;
    }
    out << " | "
        << "0x" << hex << setfill('0') << setw(16) << msg.address << " | "
        << dec << setfill(' ') << right
        << setw(6) << msg.tokens << " | "
        << setw(4) << msg.hops << " | "
        << endl;
}

void COMA::Node::Interface::Print(std::ostream& out) const
{
    static const struct {
        const char* name;
        const Buffer<Message*> COMA::Node::Interface::*buffer;
    } Buffers[2] = {
        {"Incoming", &Interface::incoming},
        {"Outgoing", &Interface::outgoing}
    };
        
    for (int i = 0; i < 2; ++i)
    {
        out <<
        "+------------------------------------------------------------+\n"
        "|                           " << Buffers[i].name << "                         |\n"
        "+-----------------------+--------------------+--------+------+\n"
        "|         Type          |       Address      | Tokens | Hops |\n"
        "+-----------------------+--------------------+--------+------+\n";
        
        const Buffer<Message*>& buffer = this->*Buffers[i].buffer;
        for (Buffer<Message*>::const_iterator p = buffer.begin(); p != buffer.end(); ++p)
        {
            PrintMessage(out, **p);
        }
        out << "+-----------------------+--------------------+--------+------+\n\n";
    }
}

void COMA::Node::Initialize(Node* next, Node* prev)
{
    m_prev.node = prev;
    m_next.node = next;
}

// Receive a message from the previous node
bool COMA::Node::ReceiveMessagePrev(Message* msg)
{
    assert(msg != NULL);
    if (!m_prev.incoming.Push(msg))
    {
        return false;
    }
    return true;
}

// Receive a message from the next node
bool COMA::Node::ReceiveMessageNext(Message* msg)
{
    assert(msg != NULL);
    if (!m_next.incoming.Push(msg))
    {
        return false;
    }
    return true;
}

COMA::Node::Node(const std::string& name, COMA& parent)
    : Simulator::Object(name, parent),
      COMA::Object(name, parent),
      m_prev(*this, "m_prev"),
      m_next(*this, "m_next")
{
    g_References++;
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
