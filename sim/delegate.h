// -*- c++ -*-
#ifndef DELEGATE_H
#define DELEGATE_H

namespace Simulator
{
    // delegate_gen: a wrapper for a bound method.
    //
    // Used e.g. in Simulator::Process to call back cycle handler
    // methods on model objects.
    //
    // This implements functionality similar to C++11's
    // std::function. The reason why a custom class is used instead of
    // std::function is that the callback method does not change
    // throughout the lifetime of delegate objects, so we can make it
    // a static parameter, let the C++ compiler inline the method in
    // delegate::method_stub and this results in *much* faster/shorter
    // instruction sequences to issue the callback.
    //
    template<typename R, typename... Args>
    class delegate_gen
    {
    protected:
        // The wrapper around the delegate method.
        R        (*m_stub)(void*, Args...);
        // The object on which the method is called.
        void     *m_object;
        // Additional arguments, see closure
        void     (*m_deleter)(void *);

        delegate_gen(R (*m)(void*, Args...), void *o, void (*d)(void*))
            : m_stub(m), m_object(o), m_deleter(d) {}

    public:
        // The implementation of the method wrapper.  This is
        // instantiated on demand by the create() constructor below.
        template <typename R_, typename... Args_>
        struct adapter
        {
            template <typename T, R_ (T::*TMethod)(Args_...)>
            static R method_stub(void* object, Args... params)
            {
                T* p = static_cast<T*>(object);
                return (p->*TMethod)(static_cast<Args_>(params)...);
            }

            template <typename T, R_ (T::*TMethod)(Args_...)>
            static delegate_gen create(T& object)
            {
                return delegate_gen{ &adapter::method_stub<T, TMethod>, &object, 0 };
            }
        };

        // The delegate constructor. For a class
        // F with method F::foo(), use as follows:
        //
        //    delegate::create<F, &F::foo>(obj);
        //
        template <typename T, R (T::*TMethod)(Args...)>
        static delegate_gen create(T& object)
        {
            return delegate_gen{ &adapter<R, Args...>::template method_stub<T, TMethod>, &object, 0 };
        }

        // The delegate entry point.
        R operator()(Args... params) const
        {
            return (*m_stub)(m_object, params...);
        }

        // To check whether the delegate is defined.
        operator bool() const { return m_stub != 0; }

        ~delegate_gen() { if (m_deleter) m_deleter(m_object); }

        delegate_gen()
            : m_stub(0), m_object(0), m_deleter(0) {}
        delegate_gen(const delegate_gen& other) = delete;
        delegate_gen(delegate_gen&& other)
            : m_stub(other.m_stub), m_object(other.m_object), m_deleter(other.m_deleter)
        { other.m_deleter = 0; }
        delegate_gen& operator=(const delegate_gen& other) = delete;
        delegate_gen& operator=(delegate_gen&& other)
        { m_stub = other.m_stub; m_object = other.m_object; m_deleter = other.m_deleter;
            other.m_deleter = 0; return *this; }
    };

}

#endif
