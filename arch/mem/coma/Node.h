#ifndef COMA_NODE_H
#define COMA_NODE_H

#include "COMA.h"

namespace Simulator
{

/**
 * void TraceWrite(MemAddr address, const char* fmt, ...);
 *
 * Note: The argument for 'fmt' has to be a string literal.
 *
 * For use in COMA::Object instances. Writes out a message
 * if the specified address is being traced.
 */
#define TraceWrite(addr, fmt, ...) do {                                 \
        if (                                                            \
            ((GetKernel()->GetDebugMode() & Kernel::DEBUG_MEMNET) ||    \
             (m_parent.GetTraces().find(addr) != m_parent.GetTraces().end())) \
            && GetKernel()->GetCyclePhase() == PHASE_COMMIT)            \
            DebugSimWrite_(("0x%llx: " fmt), (unsigned long long)(addr), ##__VA_ARGS__); \
    } while (false)

/**
 * This class defines a generic ring node interface.
 * Both directories and caches inherit from this. This way, a heterogeneous
 * COMA hierarchy can be easily composed, since no element needs to know the
 * exact type of its neighbour.
 */
class COMA::Node : public COMA::Object
{
protected:
    friend class COMA::Directory;

    /// This is the message that gets sent around
    union Message
    {
        enum Type {
            REQUEST,            ///< Read request (RR)
            REQUEST_DATA,       ///< Read request with data (RD)
            REQUEST_DATA_TOKEN, ///< Read request with data and tokens (RDT)
            EVICTION,           ///< Eviction (EV)
            UPDATE,             ///< Update (UP)
        };

        /// The actual message contents that's simulated
        struct
        {
            Type         type;          ///< Type of message
            bool         dirty;         ///< Is the data dirty? (EV, RD, RDT)
            bool         ignore;        ///< Just pass this message through to the top level
            MemAddr      address;       ///< The address of the cache-line
            MemData      data;          ///< The data (RD, RDT, EV, UP)
            NodeID       sender;        ///< ID of the sender of the message
            size_t       client;        ///< Sending client (UP)
            WClientID    wid;           ///< Sending entity on client (family/thread) (UP)
            unsigned int tokens;        ///< Number of tokens in this message (RDT, EV)
        };

        /// For memory management
        Message* next;

        // Overload allocation for efficiency
        static void * operator new (size_t size);
        static void operator delete (void *p, size_t size);

        std::string str() const;

        Message() {}
    private:
        Message(const Message&) {} // No copying
    };

private:
    // Message management
    static Message*            g_FreeMessages;
    static std::list<Message*> g_Messages;
    static unsigned long       g_References;

    NodeID            m_id;             ///< Node identifier in the memory network
    Node*             m_prev;           ///< Prev node in the ring
    Node*             m_next;           ///< Next node in the ring
protected:
    Buffer<Message*>  m_incoming;       ///< Buffer for incoming messages from the prev node
private:
    Buffer<Message*>  m_outgoing;       ///< Buffer for outgoing messages to the next node

    /// Process for sending to the next node
    Process           p_Forward;
    Result            DoForward();

protected:
    StorageTraceSet GetOutgoingTrace() const {
        return m_outgoing;
    }

    /// Send the message to the next node
    bool SendMessage(Message* message, size_t min_space);

    /// Print a message queue
    static void Print(std::ostream& out, const std::string& name, const Buffer<Message*>& buffer);

    /// Print the incoming and outgoing buffers of this node
    void Print(std::ostream& out) const;

    /// Construct the node
    Node(const std::string& name, COMA& parent, Clock& clock, NodeID id, Config& config);
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    virtual ~Node();

    /// For iterating during directory initialization
    Node* GetNextNode() const { return m_next; }
    Node* GetPrevNode() const { return m_prev; }
    NodeID GetNodeID() const { return m_id; }

public:
    void Connect(Node* next, Node* prev);
};

}
#endif
