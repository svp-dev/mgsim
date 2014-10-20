// -*- c++ -*-
#ifndef SIM_REGISTER_H
#define SIM_REGISTER_H

#include <sim/storage.h>
#include <sim/sampling.h>

namespace Simulator
{

    // A full/empty single-value storage element.
    template <typename T>
    class Register : public SensitiveStorage
    {
        DefineStateVariable(T   , cur);
        DefineStateVariable(T   , new);
        DefineStateVariable(bool, empty);
        DefineStateVariable(bool, cleared);
        DefineStateVariable(bool, assigned);

    protected:
        // Update: update the register between cycles.
        void Update() override;

    public:
        // Empty: returns true iff the register is currently empty.
        bool Empty() const;

        // Access the register.
        const T& Read() const;

        // Flush the register back to empty.
        void Clear();

        // Write data to the register.
        // If going from empty to full, activate the sensitive process.
        void Write(const T& data);

        // Constructor
        Register(const std::string& name, Object& parent, Clock& clock);

        Register(const Register&) = delete;
        Register& operator=(const Register&) = delete;
        static constexpr const char* NAME_PREFIX = "r_";
    };

}

#include "sim/register.hpp"

#endif
