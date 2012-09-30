#include <sys_config.h>

#if defined(TARGET_MTALPHA)
#include "ISA.mtalpha.cpp"
#elif defined(TARGET_MTSPARC)
#include "ISA.mtsparc.cpp"
#elif defined(TARGET_MIPS32) || defined(TARGET_MIPS32EL)
#include "ISA.mips.cpp"
#endif



