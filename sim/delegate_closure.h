// -*- c++ -*-
#ifndef DELEGATE_CLOSURE_H
#define DELEGATE_CLOSURE_H

#include <tuple>
#include <utility>

namespace Simulator
{
    namespace closure_helpers {

        template<int ...>
        struct seq { };

        template<int N, int ...S>
        struct gens : gens<N-1, N-1, S...> { };

        template<int ...S>
        struct gens<0, S...> {
            typedef seq<S...> type;
        };
    }

    template<typename R, typename... Args>
    struct closure {

        template<typename R_, typename...Args_>
        struct adapter
        {
        template<typename...CapturedArgs>
        struct capture : delegate_gen<R, Args...> {
        protected:
            template<typename T, R_ (T::*TMethod)(CapturedArgs..., Args_...)>
            struct wrap;

            using delegate_gen<R, Args...>::delegate_gen;
        public:

            template<typename T, R_ (T::*TMethod)(CapturedArgs..., Args_...)>
            static delegate_gen<R, Args...> create(T& object, CapturedArgs... args)
            {
                typedef wrap<T, TMethod> wrap_t;
                return capture{ &wrap_t::method_stub,
                        new wrap_t{object, std::forward_as_tuple(args...)},
                        &wrap_t::deleter_stub };
            }
        };
        };
    };

    template<typename R, typename... Args>
    template<typename R_, typename... Args_>
    template<typename...CapturedArgs>
    template<typename T, R_ (T::*TMethod)(CapturedArgs..., Args_...)>
    class closure<R, Args...>::adapter<R_, Args_...>::capture<CapturedArgs...>::wrap {
        friend class capture;

        wrap(T& object, std::tuple<CapturedArgs...>&& cargs)
            : m_object(&object),
              m_args(cargs)
        {}

        T* m_object;
        std::tuple<CapturedArgs...> m_args;

        static R method_stub(void* self, Args... rargs)
        {
            const wrap *s = static_cast<wrap*>(self);
            return do_call(s->m_object,
                           std::tuple_cat(s->m_args,
                                          std::forward_as_tuple(static_cast<Args_>(rargs)...)));
        }

        static void deleter_stub(void *self)
        {
            delete static_cast<wrap*>(self);
        }

        template<typename... RArgs>
        static R do_call(T* object, std::tuple<RArgs...>&& rargs)
        {
            return do_call1(object,
                            std::move(rargs),
                            typename closure_helpers::gens<sizeof...(RArgs)>::type());
        }

        template<typename... RArgs, int... S>
        static R do_call1(T* object,
                          std::tuple<RArgs...>&& rargs,
                          closure_helpers::seq<S...>)
        {
            return (object->*TMethod)(std::get<S>(rargs)...);
        }
    };

}

#endif
