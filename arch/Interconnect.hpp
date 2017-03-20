#ifndef INTERCONNECT_HPP
#define INTERCONNECT_HPP

#ifndef INTERCONNECT_H
#error This file should be included in interconnect.h
#endif

#include <cstring>

namespace Simulator
{
    namespace IC
    {
        template<typename Payload>
        IInterconnect<Payload>::~IInterconnect() {}

        template<typename Payload>
        Message<Payload>* MessagePool<Payload>::g_FreeMessages = 0;
        template<typename Payload>
        std::vector<Message<Payload>*> MessagePool<Payload>::g_Messages;

        template<typename Payload>
        inline void Message<Payload>::operator delete(void *p)
        {
            MessagePool<Payload>::free(static_cast<Message<Payload>*>(p));
        }

        template<typename Payload>
        Message<Payload>* Message<Payload>::dup() const
        {
            return new Message<Payload>(static_cast<const Payload&>(*this));
        }

        template<typename Payload>
        template<typename...Args>
        Message<Payload>::Message(Args&&... args)
            : Payload(std::forward<Args>(args)...)
              {}

        template<typename Payload>
        inline void MessagePool<Payload>::free(Message<Payload>* msg)
        {
#ifndef NDEBUG
            memset(msg, 0xFE, sizeof(*msg));
#endif
            msg->next = g_FreeMessages;
            g_FreeMessages = msg;
        }

        template<typename Payload>
        void *Message<Payload>::operator new(size_t /* unused */)
        {
            return MessagePool<Payload>::alloc();
        }

        template<typename Payload>
        Message<Payload>* MessagePool<Payload>::alloc()
        {
            // We allocate this many messages at once
            constexpr size_t ALLOCATE_SIZE = 1024;
            if (g_FreeMessages == NULL)
            {
                // Allocate more messages
                Message<Payload>* msg = new Message<Payload>[ALLOCATE_SIZE];
                g_Messages.push_back(msg);

                for (size_t i = 0; i < ALLOCATE_SIZE; ++i, ++msg)
                {
                    msg->next = g_FreeMessages;
                    g_FreeMessages = msg;
                }
            }
            Message<Payload>* msg = g_FreeMessages;
            g_FreeMessages = msg->next;

            return msg;
        }

    }
}

#endif
