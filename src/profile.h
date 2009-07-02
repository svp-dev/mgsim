#ifndef PROFILE_H
#define PROFILE_H

#include "types.h"
#include <string>
#include <map>

#ifdef PROFILE
void PROFILE_BEGIN(const std::string& name);
void PROFILE_END(const std::string& name);
static bool inline ProfilingEnabled() { return true; }
#else
static void inline PROFILE_BEGIN(const std::string& /* name */) {}
static void inline PROFILE_END(const std::string& /* name */) {}
static bool inline ProfilingEnabled() { return false; }
#endif

typedef std::map<std::string, uint64_t> ProfileMap;

uint64_t GetProfileTime(const std::string& name);
const ProfileMap& GetProfiles();

#endif

