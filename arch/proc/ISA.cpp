#include <sys_config.h>

#if defined(TARGET_MTALPHA)
#include "ISA.mtalpha.cpp"
#elif defined(TARGET_MTSPARC)
#include "ISA.mtsparc.cpp"
#endif

