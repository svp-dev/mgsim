#include "sim/configmap.h"
#include <algorithm>

using namespace std;

void ConfigMap::append(const string& key_, const string& val)
{
    m_map.push_back(make_pair(key_, val));
    auto& key = m_map[m_map.size()-1].first;
    transform(key.begin(), key.end(), key.begin(), ::tolower);
}
