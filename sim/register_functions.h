#ifndef SIM_REGISTER_FUNCTIONS_H
#define SIM_REGISTER_FUNCTIONS_H

#include <sim/sampling.h>

namespace Simulator
{

#define RegisterSampleVariable(var, ...) \
    GetKernel()->GetVariableRegistry().RegisterVariable(var, ##__VA_ARGS__)

#define RegisterSampleVariableInObject(var, cat, ...)                       \
    RegisterSampleVariable(var, GetName() + ':' + (#var + 2) , cat, ##__VA_ARGS__)

#define RegisterSampleVariableInObjectWithName(var, name, cat, ...)     \
    RegisterSampleVariable(var, GetName() + ':' + name, cat, ##__VA_ARGS__)

#define DefineSampleVariable(Type, Member) \
    Type m_ ## Member

#define DefineStateVariable(Type, Member) \
    Type m_ ## Member

#define RegisterStateVariable(LValue, Name) \
    RegisterSampleVariableInObjectWithName(LValue, Name, SVC_STATE)

#define RegisterStateObject(LValue, Name) \
    RegisterSampleVariableInObjectWithName(&(LValue), Name, SVC_STATE, Serialization::SV_OTHER, 0, 0, \
                                           &Serialization::serializer<StreamSerializer, typename std::remove_reference<decltype(LValue)>::type>)

#define RegisterStateArray(LValue, Size, Name) \
    RegisterSampleVariableInObjectWithName(LValue, Name, SVC_STATE, Size)

#define InitSampleVariable(Member, ...)                             \
    m_ ## Member((RegisterSampleVariableInObject(m_ ## Member, ##__VA_ARGS__), 0))

#define InitStateVariable(Member, ...)                                 \
    m_ ## Member((RegisterSampleVariableInObject(m_ ## Member, SVC_STATE), ##__VA_ARGS__))
}

#endif
