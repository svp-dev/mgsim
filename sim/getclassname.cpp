#include "sim/types.h"
#include "sim/getclassname.h"

#include <cstdlib>

#ifdef HAVE_GCC_ABI_DEMANGLE
#include <cxxabi.h>
#endif

using namespace std;

string GetClassName(const type_info& info)
{
    const char* name = info.name();

    int status = 0;
    char *res = 0;

#ifdef HAVE_GCC_ABI_DEMANGLE
    res = abi::__cxa_demangle(name, NULL, NULL, &status);
#endif

    if (res && status == 0)
    {
        string ret = res;
        free(res);
        return ret;
    }
    else
    {
        return name;
    }
}
