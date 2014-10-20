// -*- c++ -*-
#ifndef SIM_SERIALIZE_H
#define SIM_SERIALIZE_H

#include <type_traits>
#include <cstddef>
#include <vector>
#include <deque>
#include <map>

namespace Simulator
{
    // c++ misses is_bool, so define it here.
    template <typename T>
    using is_bool = std::is_same<typename std::remove_cv<T>::type, bool>;

    namespace Serialization
    {
        enum SerializationValueType {
            SV_BOOL,    // boolean
            SV_INTEGER, // fixed-width integer value
            SV_FLOAT,   // fixed-width floating-point
            SV_BINARY,  // fixed-width binary (char array)
            SV_OTHER    // other format
        };

        template<typename T>
        struct simple_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& val)
            {
                static_assert(std::is_floating_point<T>::value || is_bool<T>::value || std::is_integral<T>::value,
                              "no serialization defined for this type");
                SerializationValueType t;
                if (std::is_floating_point<T>::value)
                    t = SV_FLOAT;
                else if (is_bool<T>::value)
                    t = SV_BOOL;
                else if (std::is_integral<T>::value)
                    t = SV_INTEGER;
                arch.serialize_raw(t, &val, sizeof(T));
            }
            virtual ~simple_serializer() {}; // unused; present to kill a gcc warning
        };

        template<typename T>
        struct enum_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& val)
            {
                arch.serialize_raw(SV_INTEGER, &val, sizeof(T));
            }
            virtual ~enum_serializer() {}; // unused; present to kill a gcc warning
        };

        template<typename T>
        struct method_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& val)
            { val.serialize(arch); }
            virtual ~method_serializer() {}; // unused; present to kill a gcc warning
        };

        template<typename T>
        struct serialize_trait :
         public std::conditional<
            std::is_arithmetic<typename std::remove_cv<T>::type>::value,
            simple_serializer<T>,
            typename std::conditional<
                std::is_enum<typename std::remove_cv<T>::type>::value,
                enum_serializer<T>,
                method_serializer<T> >::type >::type
        {};

        template<typename A, typename T>
        static void serializer(A& arch, void* data)
        {
            T* v = static_cast<T*>(data);
            serialize_trait<T>::serialize(arch, *v);
        }

        template<typename T, char C>
        struct container_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& container)
            {
                size_t sz = container.size();
                char tag[2] = {C, 0};
                arch & (&tag[0]) & sz;
                container.resize(sz);
                for (auto& v : container)
                    arch & v;
            }
            virtual ~container_serializer() {}; // unused; present to kill a gcc warning
        };

        template<typename T>
        struct pair_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& p)
            {
                arch & "pf" & p.first & "ps" & p.second;
            }
            virtual ~pair_serializer() {}; // unused; present to kill a gcc warning
        };

        template<typename T>
        struct map_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& container)
            {
                size_t sz = container.size();
                std::vector<std::pair<typename T::key_type, typename T::mapped_type> > vec(sz);

                if (arch.reading)
                    std::copy(container.begin(), container.end(), vec.begin());

                arch & vec;

                if (arch.reading)
                    return;

                container.clear();
                for (auto & p : vec)
                    container[p.first] = p.second;
            }
            virtual ~map_serializer() {}; // unused; present to kill a gcc warning
        };

        template<typename T>
        struct serialize_trait<std::vector<T> > : public container_serializer<std::vector<T>, 'v'> {};
        template<typename T>
        struct serialize_trait<std::deque<T> > : public container_serializer<std::deque<T>, 'q'> {};
        template<typename K, typename T>
        struct serialize_trait<std::map<K,T> > : public map_serializer<std::map<K,T> > {};
        template<typename K, typename T>
        struct serialize_trait<std::pair<K,T> > : public pair_serializer<std::pair<K,T> > {};

#define SERIALIZE(Arch) template<typename A> void serialize(A& Arch)

        struct binary_serializer
        {
            void *p;
            size_t sz;
        };

        template<typename T>
        binary_serializer binary(T *p, size_t sz)
        { return binary_serializer{p, sizeof (T) * sz}; }

        template<typename A>
        inline
        A& operator&(A& s, const binary_serializer& bs)
        {
            s.serialize_raw(SV_BINARY, bs.p, bs.sz);
            return s;
        }

    }
}

#endif
