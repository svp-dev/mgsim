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
        // The wrapper around the delegate method.
        R        (*m_stub)(void*, Args...);
        // The object on which the method is called.
        void     *m_object;

        // The implementation of the method wrapper.  This is
        // instantiated on demand by the create() constructor below.
        template <typename T, R (T::*TMethod)(Args...)>
        static R method_stub(void* object, Args... params)
        {
            T* p = static_cast<T*>(object);
            return (p->*TMethod)(params...);
        }

        delegate_gen(R (*m)(void*, Args...), void *o)
            : m_stub(m), m_object(o) {}

    public:
        // The delegate constructor. For a class
        // F with method F::foo(), use as follows:
        //
        //    delegate::create<F, &F::foo>(obj);
        //
        template <typename T, R (T::*TMethod)(Args...)>
        static delegate_gen create(T& object)
        {
            return delegate_gen{ &method_stub<T, TMethod>, &object };
        }

        // The delegate entry point.
        R operator()(Args... params) const
        {
            return (*m_stub)(m_object, params...);
        }
    };

}

#endif
