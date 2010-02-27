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
#define TraceWrite(addr, fmt, ...) do { \
        const COMA::TraceMap& traces = m_parent.GetTraces(); \
        if (!traces.empty() && traces.find((addr)) != traces.end()) { \
            OutputWrite(("0x%llx: " fmt), (unsigned long long)(addr), ##__VA_ARGS__); \
        } \
    } while (false)

/**
 * This class defines a generic ring node interface.
 * Both directories and caches inherit from this. This way, a heterogeneous
 * COMA hierarchy can be easily composed, since no element needs to know the
 * exact type of its neighbour.
 */
class COMA::Node : public virtual COMA::Object
{
protected:
    /***
     * This is the message that gets sent around.
     *
     * Some details on the meaning of the fields for various types.
     *
     * 'address' always indicates the cache-line the message concerns.
     * 
     * For requests, the 'hops' field is incremented whenever the request
     * misses a cache and/or directory. For responses, it designates the
     * destination by being decremented and forwarded until it's zero.
     * Responses are routed purely on this counter.
     *
     * 'data':
     * Only has a meaning for RESPONSE_READ and holds the data that has
     * been read.
     * 
     * 'tokens':
     * For REQUEST_READ and REQUEST_UPDATE it has no meaning (should be 0).
     * For REQUEST_EVICT it indicates how many tokens the eviction carries.
     * For REQUEST_KILL_TOKENS it indicates how many tokens to kill.
     * For RESPONSE_READ it indicates how many tokens the destination
     * cache got from the sender.
     * For RESPONSE_FORWARD it indicates how many hops are between the
     * destination and the next cache that the destination should forward
     * its data to.
     */
    union Message
    {
        enum Type {
            REQUEST_READ,       ///< Read request (RR)
            REQUEST_EVICT,      ///< Eviction request (merge tokens) (ER)
            REQUEST_KILL_TOKENS,///< Token-Kill request (TKR)
            REQUEST_UPDATE,     ///< Update request (UR)
            RESPONSE_READ,      ///< Read response with data and tokens (DR)
            RESPONSE_FORWARD,   ///< Forward-update response (FR)
        };
        
        /// The actual message contents that's simulated
        struct
        {
            Type         type;      ///< Type of message
            MemAddr      address;   ///< The address of the cache-line
            MemData      data;      ///< The data (ER, DR)
            bool         dirty;     ///< Is the data dirty? (ER)
            unsigned int hops;      ///< How many caches did this message pass?
            unsigned int tokens;    ///< Number of tokens in this message (ER, TKR, DR, FR)
            size_t       client;    ///< Sending client (UR)
            TID          tid;       ///< Sending thread (UR)
        };
        
        /// For memory management
        Message* next;

        // Overload allocation for efficiency
        static void * operator new (size_t size);
        static void operator delete (void *p, size_t size);
        
        Message() {}
    private:
        Message(const Message&) {} // No copying
    };
    
    // Message management
    static Message*            g_FreeMessages;
    static std::list<Message*> g_Messages;
    static unsigned long       g_References;

    class Interface
    {
        static void PrintMessage(std::ostream& out, const Message& msg);
    
    public:
        Node*             node;
        Buffer<Message*>  incoming;
        Buffer<Message*>  outgoing;
        ArbitratedService arbitrator;
        
        bool Send(Message* message);
        void Print(std::ostream& out) const;
        
        Interface(const Object& object, const std::string& name);
    };
    
    Interface m_prev;
    Interface m_next;
    
    Node(const std::string& name, COMA& parent);
    virtual ~Node();

public:
    bool ReceiveMessagePrev(Message* msg);
    bool ReceiveMessageNext(Message* msg);

    void Initialize(Node* next, Node* prev);
};

}
#endif
