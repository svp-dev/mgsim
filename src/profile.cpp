#include "profile.h"
#include "simtime.h"
using namespace std;

static ProfileMap g_TotalProfiles;
static ProfileMap g_ActiveProfiles;

#ifdef PROFILE
void PROFILE_BEGIN(const string& name)
{
    g_ActiveProfiles[name] = GetTime();
}

void PROFILE_END(const string& name)
{
    ProfileMap::const_iterator p = g_ActiveProfiles.find(name);
    if (p != g_ActiveProfiles.end())
    {
        uint64_t t = GetTime() - p->second;
        ProfileMap::iterator p = g_TotalProfiles.find(name);
        if (p != g_TotalProfiles.end()) {
            p->second += t;
        } else {
            g_TotalProfiles.insert(make_pair(name, t));
        }
    }
}
#endif

uint64_t GetProfileTime(const string& name)
{
    ProfileMap::const_iterator p = g_TotalProfiles.find(name);
    return (p != g_TotalProfiles.end()) ? p->second : 0;
}

const ProfileMap& GetProfiles()
{
    return g_TotalProfiles;
}
