// -*- c++ -*-
#ifndef SIM_OBJECT_H
#define SIM_OBJECT_H

#ifndef KERNEL_H
#error This file should be included in kernel.h
#endif

#include <string>
#include <vector>
#include "sim/types.h"

namespace Simulator
{
    class Kernel;

    /**
     * @brief Base class for simulator components.
     * The Object class is the base object for all simulated components, offering
     * interaction with the system. Objects are linked in a hierarchy and each
     * is managed by a kernel, to which it must register. It will unregister from
     * its kernel when destroyed.
     */
    class Object
    {
        Object*              m_parent;      ///< Parent object.
        std::string          m_name;        ///< Object name.
#ifndef STATIC_KERNEL
        Kernel&              m_kernel;      ///< The kernel that manages this object.
#endif
        std::vector<Object*> m_children;    ///< Children of this object.

        Object(const Object&) = delete; // No copy.
        Object& operator=(const Object&) = delete; // No assignment.

    public:
        /**
         * Constructs a root object.
         * @param name the name of this object.
         */
        Object(const std::string& name, Kernel& k);

        /**
         * Constructs a child object, using the same kernel as the parent
         * @param parent the parent object.
         * @param name the name of this object.
         */
        Object(const std::string& name, Object& parent);

        virtual ~Object();

        /// Check if the simulation is in the acquiring phase.
        bool IsAcquiring() const;
        /// Check if the simulation is in the check phase.
        bool IsChecking() const;
        /// Check if the simulation is in the commit phase.
        bool IsCommitting() const;

        /// Get the kernel managing this object. @return the kernel managing this object.
#ifdef STATIC_KERNEL
        static Kernel* GetKernel();
#else
        Kernel* GetKernel() const;
#endif

        /// Get the parent object. @return the parent object.
        Object*            GetParent() const { return m_parent; }
        /// Get the number of children of the object. @return the number of children.
        unsigned int       GetNumChildren() const { return (unsigned int)m_children.size(); }
        /// Get a child of the object. @param i the index of the child. @return the child at index i.
        Object*            GetChild(int i)  const { return m_children[i]; }
        /// Get the object fully qualified name. @return the object name.
        const std::string& GetName()   const { return m_name; }

        /**
         * @brief Writes simulator debug output.
         * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_SIM.
         * @param msg the printf-style format string.
         */
        void DebugSimWrite_(const char* msg, ...) const FORMAT_PRINTF(2,3);

        /**
         * @brief Writes program debug output.
         * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_PROG.
         * @param msg the printf-style format string.
         */
        void DebugProgWrite_(const char* msg, ...) const FORMAT_PRINTF(2,3);

        /**
         * @brief Writes deadlock debug output.
         * Writes debug output if and only if the object's kernel's debug more contains at least DEBUG_DEADLOCK.
         * @param msg the printf-style format string.
         */
        void DeadlockWrite_(const char* msg, ...) const FORMAT_PRINTF(2,3);

        /**
         * @brief Writes general output.
         * @param msg the printf-style format string.
         */
        void OutputWrite_(const char* msg, ...) const FORMAT_PRINTF(2,3);
    };



#define COMMIT  if (IsCommitting())

// Define these "methods" as macros to allow for optimizations
#define DebugDo_(CAT, msg, ...) \
    do { COMMIT if (GetKernel()->GetDebugMode() & Kernel::DEBUG_##CAT) DebugSimWrite_(msg, ##__VA_ARGS__); } while(false)

#define DebugSimWrite(msg, ...)   DebugDo_(SIM,   ("s " msg), ##__VA_ARGS__)
#define DebugProgWrite(msg, ...)  DebugDo_(PROG,  ("- " msg), ##__VA_ARGS__)
#define DebugFlowWrite(msg, ...)  DebugDo_(FLOW,  ("f " msg), ##__VA_ARGS__)
#define DebugMemWrite(msg, ...)   DebugDo_(MEM,   ("m " msg), ##__VA_ARGS__)
#define DebugIOWrite(msg, ...)    DebugDo_(IO,    ("i " msg), ##__VA_ARGS__)
#define DebugRegWrite(msg, ...)   DebugDo_(REG,   ("s " msg), ##__VA_ARGS__)
#define DebugNetWrite(msg, ...)   DebugDo_(NET,   ("n " msg), ##__VA_ARGS__)
#define DebugIONetWrite(msg, ...) DebugDo_(IONET, ("b " msg), ##__VA_ARGS__)
#define DebugFPUWrite(msg, ...)   DebugDo_(FPU,   ("F " msg), ##__VA_ARGS__)
#define DebugPipeWrite(msg, ...)  DebugDo_(PIPE,  ("p " msg), ##__VA_ARGS__)

#define DeadlockWrite(msg, ...)                                         \
    do { if (GetKernel()->GetDebugMode() & Kernel::DEBUG_DEADLOCK) DeadlockWrite_((msg), ##__VA_ARGS__); } while(false)
#define OutputWrite(msg, ...)                                           \
    do { COMMIT OutputWrite_((msg), ##__VA_ARGS__); } while(false)

}

#endif
