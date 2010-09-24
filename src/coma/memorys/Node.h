#ifndef ZLCOMA_NODE_H
#define ZLCOMA_NODE_H

#include "COMA.h"

namespace Simulator
{

/**
 * void TraceWrite(MemAddr address, const char* fmt, ...);
 *
 * Note: The argument for 'fmt' has to be a string literal.
 *
 * For use in ZLCOMA::Object instances. Writes out a message
 * if the specified address is being traced.
 */
#ifdef TraceWrite
#undef TraceWrite
#endif
#define TraceWrite(addr, fmt, ...) do { \
        const ZLCOMA::TraceMap& traces = m_parent.GetTraces(); \
        MemAddr __addr = (addr) / m_lineSize * m_lineSize; \
        if (!traces.empty() && traces.find((__addr)) != traces.end()) { \
            OutputWrite(("0x%llx: " fmt), (unsigned long long)(__addr), ##__VA_ARGS__); \
        } \
    } while (false)

/**
 * This class defines a generic ring node interface.
 * Both directories and caches inherit from this. This way, a heterogeneous
 * COMA hierarchy can be easily composed, since no element needs to know the
 * exact type of its neighbour.
 */
class ZLCOMA::Node : public virtual ZLCOMA::Object
{
protected:
    /// This is the message that gets sent around
    union Message
    {
        enum Type
        {
            ACQUIRE_TOKEN,              // AT: acquire token
            ACQUIRE_TOKEN_DATA,         // AD: acquire token & data
            DISSEMINATE_TOKEN_DATA,     // DD: disseminate token
            LOCALDIR_NOTIFICATION,      // the notification is used for caches to notify local directory the changes in the token status in the cache
        };

        /// The actual message contents that's simulated
        struct
        {
            //
            // The following fields are never changed after the message is created:
            //
            
            Type            type;       // Type of the message
            MemAddr         address;    // Address of the concerned cache-line
            CacheID         source;     // ID of the originating cache
            size_t          client;     // Sending client (writes)
            TID             tid;        // Sending thread (writes)
            
            // Number of tokens requested.
            // Possible values: 0 (non-dirty eviction), 1 (read), TotalTokens (write/dirty eviction)
            unsigned int tokenrequested;

            //
            // These fields can change:
            //

            MemData         data;
            bool            bitmask[MAX_MEMORY_OPERATION_SIZE];

            // To avoid deadlock, messages sometimes have to be routed the long way.
            // This flag, when set, causes all relevant clients to ignore the message in such a case.
            bool ignore;

            std::vector<WriteAck>* msbcopy;
            
            // Is the request preprocessed?
            bool processed;

            // Does the request have the priority token?
            bool priority;

            // If set, this request has transient tokens (tokenacquired).
            // This can only ever be true for requests of type:
            // * ACQUIRE_TOKEN (which always has tokenrequested == TotalTokens), or
            // * ACQUIRE_TOKEN_DATA with tokenrequested == TotalTokens
            bool transient;

            // Is this a merged request (from the MergeStoreBuffer)?
            bool merged;

            // Number of tokens acquired/held by this request
            unsigned int tokenacquired;

            // Is this message a reply?
            bool dataavailable;
        };

        // transient tokens cannot be grabbed by anybody, but can be transformed into permanent token by priority token
        // permanent tokens can be stored and grabed by the lines.
        unsigned int gettokenpermanent() const
        {
            return transient ? 0 : tokenacquired;
        }

        /// For memory management
        Message* next;

        // Overload allocation for efficiency
        static void * operator new (size_t size);
        static void operator delete (void *p, size_t size);

        Message() { msbcopy = NULL; }
        ~Message() { delete msbcopy; }
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
    /// Send the message to the next node
    bool SendMessage(Message* message, size_t min_space);
    
    /// Print the incoming and outgoing buffers of this node
    void Print(std::ostream& out) const;
    
    /// Construct the node
    Node(const std::string& name, ZLCOMA& parent, Clock& clock);
    virtual ~Node();

public:
    void Initialize(Node* next, Node* prev);
};

}
#endif
