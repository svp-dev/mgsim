// -*- c++ -*-
#ifndef ZLCDMA_NODE_H
#define ZLCDMA_NODE_H

#include "CDMA.h"

namespace Simulator
{

/**
 * void TraceWrite(MemAddr address, const char* fmt, ...);
 *
 * Note: The argument for 'fmt' has to be a string literal.
 *
 * For use in ZLCDMA::Object instances. Writes out a message
 * if the specified address is being traced.
 */
#ifdef TraceWrite
#undef TraceWrite
#endif
#define TraceWrite(addr, fmt, ...) do { \
        const ZLCDMA::TraceMap& traces = m_parent.GetTraces(); \
        MemAddr __addr = (addr) / m_lineSize * m_lineSize; \
        if (!traces.empty() && traces.find((__addr)) != traces.end()) { \
            OutputWrite(("0x%llx: " fmt), (unsigned long long)(__addr), ##__VA_ARGS__); \
        } \
    } while (false)

/**
 * This class defines a generic ring node interface.
 * Both directories and caches inherit from this. This way, a heterogeneous
 * CDMA hierarchy can be easily composed, since no element needs to know the
 * exact type of its neighbour.
 */
class ZLCDMA::Node : public ZLCDMA::Object
{
protected:
    friend class ZLCDMA::Directory;

    /// This is the message that gets sent around
    union Message
    {
        enum Type
        {
            READ,                       // RR: read request
            ACQUIRE_TOKENS,             // AT: acquire all tokens (and optionally, data)
            EVICTION,                   // EV: disseminate token
            LOCALDIR_NOTIFICATION,      // the notification is used for caches to notify local directory the changes in the token status in the cache
        };

        /// The actual message contents that's simulated
        struct
        {
            Type            type;       // Type of the message
            MemAddr         address;    // Address of the concerned cache-line
            CacheID         source;     // ID of the originating cache
            bool            dirty;      // Is the data in this message 'dirty'?

            // Message data and validity bitmask
            char            data   [MAX_MEMORY_OPERATION_SIZE];
            bool            bitmask[MAX_MEMORY_OPERATION_SIZE];

            // To avoid deadlock, messages sometimes have to be routed the long way.
            // This flag, when set, causes all relevant clients to ignore the message in such a case.
            bool ignore;

            // Does the request have the priority token?
            bool priority;

            // If set, this request has transient tokens.
            // This can only ever be true for ACQUIRE_TOKENS messages.
            // If priority is true, transient cannot be.
            bool transient;

            // Number of tokens held by this request
            unsigned int tokens;

            // (See also serializer below!)
        };

        // transient tokens cannot be grabbed by anybody, but can be transformed into permanent token by priority token
        // permanent tokens can be stored and grabed by the lines.
        unsigned int gettokenpermanent() const
        {
            return transient ? 0 : tokens;
        }

        /// For memory management
        Message* next;

        // Overload allocation for efficiency
        static void * operator new (size_t size);
        static void operator delete (void *p, size_t size);

        Message() {};
    private:
        Message(const Message&) {} // No copying
    };

private:
    // Message management
    static Message*            g_FreeMessages;
    static std::list<Message*> g_Messages;
    static unsigned long       g_References;

    static void PrintMessage(std::ostream& out, const Message& msg);

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
    Node(const std::string& name, ZLCDMA& parent, Clock& clock);
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    virtual ~Node();

public:
    void Initialize(Node* next, Node* prev);
};

namespace Serialization
{
    template<>
    struct serialize_trait<ZLCDMA::Node::Message*>
    {
        template<typename A>
        static void serialize(A& arch, ZLCDMA::Node::Message* &p)
        {
            if (p == NULL)
                p = new ZLCDMA::Node::Message;
            arch & "[cn"
                & p->type
                & p->address
                & p->source
                & p->dirty
                & p->ignore
                & p->priority
                & p->transient
                & p->tokens
                & Serialization::binary(p->data, MAX_MEMORY_OPERATION_SIZE);
            for (auto &b : p->bitmask) arch & b;
            arch & "]";
        }
    };
}

}
#endif
