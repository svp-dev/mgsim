#ifndef UNREACHABLE_H
#define UNREACHABLE_H

#include <sys_config.h>
#include <cassert>

#ifdef HAVE___BUILTIN_UNREACHABLE
#define __UNREACHABLE __builtin_unreachable()
#else
#define __UNREACHABLE (void)0
#endif

#define UNREACHABLE do {                        \
        assert(false && "unreachable");         \
        __UNREACHABLE;                          \
    } while(1);


#endif
