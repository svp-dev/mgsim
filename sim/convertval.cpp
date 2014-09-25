#include <algorithm>
#include <cctype>
#include "sim/convertval.h"

using namespace std;

bool converter<bool>::convert(const std::string& s, size_t *p)
{
    string val(s);
    transform(val.begin(), val.end(), val.begin(), ::toupper);

    // Check for the boolean values
    if (val == "TRUE" || val == "YES") { *p = s.size(); return true; }
    if (val == "FALSE" || val == "NO") { *p = s.size(); return false; }

    // Otherwise, try to interpret as an integer
    return !!converter<unsigned>::convert(s, p);
}
