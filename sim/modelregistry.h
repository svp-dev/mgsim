// -*- c++ -*-
#ifndef MODEL_REGISTRY_H
#define MODEL_REGISTRY_H

#include "sim/kernel.h"
#include "programs/mgsim.h"

#include <ostream>
#include <utility>
#include <map>
#include <set>

/// ComponentModelRegistry: registry for the component structure and
// properties after the architecture has been set up.
//
// This is used to automatically generate a (GraphViz) block diagram
// of the configured architecture, as well as generating a (simulated)
// machine configuration ROM (see Config::GetConfWords) so that the OS
// running on the simulated architecture can introspect what
// configuration was selected.
//
// The model registry is held as:
// - a directed graph of objects references.
// - a "database" (registry) of entities
//
// Object references in turn can also be associated to a vector of
// zero or more "properties".
//
// A property is a pair (label, value) where the value is itself a
// reference to an "entity".
//
// The registry stores all entities involved in the graph. An entity
// can be either a text label (Symbol), an integer value, a
// reference to a simulation object, or "nothing" (void).
//
// The edges between nodes/entities in the graph are directed and
// associated with two boolean flags and their own vector of properties.
//
// The first boolean flag on an edge, if true, means the objects on
// both sides are logical "siblings" to each other respective to the
// overall object hierarchy.
// The second boolean flag, if true, means the logical link between
// the two objects is bidirectional, eg. to represent bidirectional
// communication.
// These two flags are mostly used when generating a graphical
// representation of the graph.
//
class ComponentModelRegistry
{

protected:
    // A symbol is a pointer to string.
    // We use memoization (makeSymbol) so as to simplify
    // symbol comparison to become a simple pointer comparison.
    typedef const std::string*          Symbol;

    // m_symbols: the set of all symbols encountered so far.
    // This is the basis for memoization by makeSymbol below.
    std::set<std::string>               m_symbols;

    // Memoize a string into a symbol.
    Symbol makeSymbol(const std::string& sym);


    // An object reference is a pointer to a simulation object
    // (no surprise here).
    typedef const Simulator::Object*    ObjectRef;

    // m_objects: registry of all objects registered so far.
    // Each object is associated with a symbol that gives it a logical "type".
    // Note this type is distinct from the actual C++ implementation
    // for the object, it is used to logically describe the object in
    // the component graph. See uses of registerObject() for examples.
    std::map<ObjectRef, Symbol>         m_objects;

public:
    // Register an object with a logical type.
    void registerObject(const Simulator::Object& obj,
                        const std::string& objtype);

protected:
    // Check an object was registered and return the corresponding
    // object reference.
    ObjectRef refObject(const Simulator::Object& obj);


    // An entity is either an integer, a symbol or an object reference.
    struct Entity
    {
        enum {
            VOID = CONF_ENTITY_VOID,
            SYMBOL = CONF_ENTITY_SYMBOL,
            OBJECT = CONF_ENTITY_OBJECT,
            UINT = CONF_ENTITY_UINT,
        } type;
        union
        {
            uint64_t     value;
            ObjectRef    object;
            Symbol       symbol;
        };

        // A comparison operator is needed so that Entity can be added
        // to std::set.
        bool operator<(const Entity& right) const;
    };

    // An entity reference is simply a pointer to an entity.  We use
    // entity references for properties so as to share the Entity
    // entry in memort if it is used by multiple properties.
    typedef const Entity*              EntityRef;

    // m_entities: registry of entities.
    // Used to create EntityRefs by refEntity().
    std::set<Entity>                   m_entities;  // registry for entities

    // Entity registration.
    EntityRef refEntity(const ObjectRef& obj);
    EntityRef refEntity(const Symbol& sym);
    EntityRef refEntity(const char* str);
    EntityRef refEntity(const std::string& str);
    EntityRef refEntity(const uint32_t& val);
    EntityRef refEntity(void);


    // A property is a (label, value) pair, with entity references as values.
    typedef std::pair<Symbol, EntityRef> Property;

    // Every object reference has a vector of associated properties.
    typedef std::map<ObjectRef, std::vector<Property> > objprops_t;
    objprops_t                         m_objprops;  // function object -> {property}*

public:
    // Property registration for objects.
    void registerProperty(const Simulator::Object& obj,
                          const std::string& name)
    {
        m_objprops[refObject(obj)]
            .push_back(std::make_pair(makeSymbol(name),
                                      refEntity()));
    };

    template<typename T>
    void registerProperty(const Simulator::Object& obj,
                          const std::string& name,
                          const T& value)
    {
        m_objprops[refObject(obj)]
            .push_back(std::make_pair(makeSymbol(name),
                                      refEntity(value)));
    };

protected:
    // The graph edges have two boolean flags and are associated
    // to their own vector of properties.
    typedef std::map<std::pair<ObjectRef,
                               std::pair<ObjectRef,
                                         std::pair<bool, bool> > >,
                     std::vector<Property> > linkprops_t;
    linkprops_t                        m_linkprops; // function (object,object,(sibling,bidir)) -> {property}*


public:
    // Edge registration.
    void registerRelation(const Simulator::Object& left,
                          const Simulator::Object& right,
                          const std::string& name,
                          bool sibling = false, bool bidi = false)
    {
        m_linkprops[std::make_pair(refObject(left),
                                   std::make_pair(refObject(right),
                                                  std::make_pair(sibling,
                                                                 bidi)))]
            .push_back(std::make_pair(makeSymbol(name),
                                      refEntity()));
    }

    template<typename T, typename U>
    void registerRelation(const T& left, const U& right,
                          const std::string& name, bool sibling = false)
    {
        registerRelation(dynamic_cast<const Simulator::Object&>(left),
                         dynamic_cast<const Simulator::Object&>(right),
                         name, sibling);
    }

    template<typename T, typename U>
    void registerBidiRelation(const T& left, const U& right,
                              const std::string& name, bool sibling = false)
    {
        registerRelation(dynamic_cast<const Simulator::Object&>(left),
                         dynamic_cast<const Simulator::Object&>(right),
                         name, sibling, true);
    }

    // object-object relations with tags

    template<typename T>
    void registerTaggedRelation(const Simulator::Object& left,
                                const Simulator::Object& right,
                                const std::string& name,
                                const T& value,
                                bool sibling = false, bool bidi = false)
    {
        m_linkprops[std::make_pair(refObject(left),
                                   std::make_pair(refObject(right),
                                                  std::make_pair(sibling,
                                                                 bidi)))]
            .push_back(std::make_pair(makeSymbol(name),
                                      refEntity(value)));
    }

    template<typename T, typename U, typename V>
    void registerTaggedRelation(const T& left, const U& right,
                                const std::string& name,
                                const V& value,
                                bool sibling = false)
    {
        registerTaggedRelation(dynamic_cast<const Simulator::Object&>(left),
                               dynamic_cast<const Simulator::Object&>(right),
                               name, value, sibling);
    }

    template<typename T, typename U, typename V>
    void registerTaggedBidiRelation(const T& left, const U& right,
                                    const std::string& name,
                                    const V& value,
                                    bool sibling = false)
    {
        registerTaggedRelation(dynamic_cast<const Simulator::Object&>(left),
                               dynamic_cast<const Simulator::Object&>(right),
                               name, value, sibling, true);
    }


protected:
    // m_names: an "internal" translation of all object references
    // to synthetic labels that are suitable for designating objects
    // in the GraphViz "dot" format.
    //
    // The names are automatically generated, after all objects have
    // been registered, by renameObjects(), in turn used by
    // dumpComponentGraph().
    std::map<ObjectRef, Symbol>        m_names;
    void renameObjects(void);

    // Entity pretty printer.  Used by dumpComponentGraph(). Must be
    // called after renameObjects().
    void printEntity(std::ostream& os, const Entity& e) const;

public:
    // Emit a representation of the component graph on the given
    // stream. The 2nd and 3rd argument tell whether to print
    // out the properties of nodes and edges, respectively.
    void dumpComponentGraph(std::ostream& out,
                            bool display_nodeprops = true,
                            bool display_linkprops = true);

    // Constructor - no surprise here.
    ComponentModelRegistry();
    virtual ~ComponentModelRegistry() {}
};


template<>
inline void
ComponentModelRegistry::registerTaggedRelation<Simulator::Object>(const Simulator::Object& left,
                                                                  const Simulator::Object& right,
                                                                  const std::string& name,
                                                                  const Simulator::Object& value,
                                                                  bool sibling,
                                                                  bool bidi)
{
    m_linkprops[std::make_pair(refObject(left),
                               std::make_pair(refObject(right),
                                              std::make_pair(sibling,
                                                             bidi)))]
        .push_back(std::make_pair(makeSymbol(name),
                                  refEntity(refObject(value))));
}



#endif
