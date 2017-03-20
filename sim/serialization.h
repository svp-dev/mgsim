// -*- c++ -*-
#ifndef SIM_SERIALIZATION_H
#define SIM_SERIALIZATION_H

#include <type_traits>
#include <vector>
#include <deque>
#include <map>
#include <cstddef>
#include <cstdint>

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
            SV_BITS,    // fixed-width binary (bool array)
            SV_OTHER    // other format
        };

        // Simple serializer for scalar types
        template<typename T>
        struct simple_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& val)
            {
                static_assert(std::is_floating_point<T>::value
                              || is_bool<T>::value
                              || std::is_integral<T>::value,
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
            virtual ~simple_serializer() {};
        };

        // Serializer for enum types.
        template<typename T>
        struct enum_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& val)
            {
                arch.serialize_raw(SV_INTEGER, &val, sizeof(T));
            }
            virtual ~enum_serializer() {};
        };

        // General serializer for objects.  This delegates to the
        // object's serialize() method, which must exist.
        template<typename T>
        struct method_serializer
        {
            template<typename A>
            static void serialize(A& arch, T& val)
            {
                val.serialize(arch);
            }
            virtual ~method_serializer() {};
        };
        // General macro to shorten the definition of
        // a serialize() method in objects.
#       define SERIALIZE(Arch) template<typename A> void serialize(A& Arch)

        // Base serializer selection.
        //
        // This uses traits to select a base class depending
        // on the type of data:
        // - simple type -> simple_serializer
        // - enum -> enum_serializer
        // - everything else -> method_serializer
        //
        // This selection is intentionally incorrect if the type is
        // neither a scalar, enum or object with a serialize() method,
        // for example when it is a pointer type or a standard
        // container. In this case, method_serializer will attempt to
        // call a serialize() method on the type, and a C++ error is
        // generated. This indicate no serialization was yet
        // implemented for the type.
        //
        // To add serialization on such types, the trait should be
        // further specialized; see below for examples.

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

        // General serialization entry point.
        // This uses specializations of serialize_trait.
        template<typename A, typename T>
        static void serializer(A& arch, void* data)
        {
            T* v = static_cast<T*>(data);
            serialize_trait<T>::serialize(arch, *v);
        }

        // Base serializer for standard sequential containers
        // (eg. std::vector, std::dequeue)
        //
        // The template parameter C is a character used to tag the
        // serialized data.
        template<typename Container, char C>
        struct container_serializer
        {
            template<typename A>
            static void serialize(A& arch, Container& container)
            {
                constexpr char tag[3] = {'[', C, 0};
                arch & (&tag[0]);

                // Serialize/deserialize the size
                size_t sz = container.size();
                arch & sz;
                container.resize(sz);

                // Serialize/deserialize the values
                for (auto& v : container)
                    arch & v;

                arch & "]";
            }
            virtual ~container_serializer() {};
        };

        // General serializer for std::vector
        template<typename T>
        struct serialize_trait<std::vector<T> >
            : public container_serializer<std::vector<T>, 'v'> {};

        // General serializer for std::deque
        template<typename T>
        struct serialize_trait<std::deque<T> >
            : public container_serializer<std::deque<T>, 'q'> {};

        // General serializer for std::vector<char>
        // (array of bytes)
        template<typename ByteType>
        struct byte_serializer
        {
            template<typename A>
            static void serialize(A& arch, std::vector<ByteType>& v)
            {
                size_t sz = v.size();
                arch & "[M" & sz;
                v.resize(sz);

                arch.serialize_raw(SV_BINARY, &v[0], sz);

                arch & "]";
            }
            virtual ~byte_serializer() {}
        };

        // General serializers for std::vector<char> and
        // std::vector<uint8_t>
        template<>
        struct serialize_trait<std::vector<char> >
            : public byte_serializer<char> {};
        template<>
        struct serialize_trait<std::vector<uint8_t> >
            : public byte_serializer<uint8_t> {};

        // Base serializer for pointer types.
        // Note that this is not enabled automatically
        // as we avoid to silently misserialize arrays.
        template<typename T>
        struct pointer_serializer
        {
            template<typename A>
            static void serialize(A& arch, T* p)
            {
                arch & *p;
            }
            virtual ~pointer_serializer() {};
        };

        // Base serializer for std::pair
        template<typename Pair>
        struct pair_serializer
        {
            template<typename A>
            static void serialize(A& arch, Pair& p)
            {
                arch & "[" & p.first & p.second & "]";
            }
            virtual ~pair_serializer() {};
        };

        // General serializer for std::pair
        template<typename K, typename T>
        struct serialize_trait<std::pair<K,T> >
            : public pair_serializer<std::pair<K,T> > {};



        // Base serializer for std::map
        template<typename Map>
        struct map_serializer
        {
            template<typename A>
            static void serialize(A& arch, Map& container)
            {
                size_t sz = container.size();
                std::vector<std::pair<typename Map::key_type,
                                      typename Map::mapped_type> > vec(sz);

                // When reading, first copy all (key,value) pairs to a
                // sequential container.
                if (arch.reading())
                    std::copy(container.begin(), container.end(),
                              vec.begin());

                // Serialize/deserialize all the (key,value) pairs.
                // This uses the default container_serializer and
                // pair_serializer logic.
                arch & vec;

                // When reading, there is nothing else to do
                if (arch.reading())
                    return;

                // Otherwise, we have new values; erase the container
                // and load it anew from the sequential container.
                container.clear();
                for (auto & p : vec)
                    container[p.first] = p.second;
            }
            virtual ~map_serializer() {};
        };

        // General serializer for std::map
        template<typename K, typename T>
        struct serialize_trait<std::map<K,T> >
            : public map_serializer<std::map<K,T> > {};


        // Helpers for the definitions of custom serialize() methods
        // in objects.

        // Serialization::binary(V, N) Serializes the array starting
        // at V and containing N elements as its byte representation.
        struct binary
        {
            void *p;
            size_t sz;

            template<typename T>
            binary(T* _p, size_t _sz)
                : p(_p), sz(_sz) {}
        };

        template<typename A>
        A& operator&(A& s, const binary& bs)
        {
            s.serialize_raw(SV_BINARY, bs.p, bs.sz);
            return s;
        }

        // Serialization::bitvec(V, N) Serializes the bool array
        // starting at V and containing N elements as its bit array
        // representation.

        struct bitvec
        {
            bool *p;
            size_t sz;

            inline
            bitvec(bool *_p, size_t _sz)
                : p(_p), sz(_sz) {}
        };

        template<typename A>
        A& operator&(A& s, const bitvec& bs)
        {
            s.serialize_raw(SV_BITS, bs.p, bs.sz);
            return s;
        }

    }
}

#endif
