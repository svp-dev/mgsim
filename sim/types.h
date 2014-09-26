// -*- c++ -*-
#ifndef TYPES_H
#define TYPES_H

// Include the configuration defines
#include <sys_config.h>

// Define the GCC version for checking for certain extensions
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#if GCC_VERSION > 20300
// The printf-format attribute is available since GCC 2.3
# define FORMAT_PRINTF(fmt,va) __attribute__ ((format (printf, fmt, va)))
#else
# define FORMAT_PRINTF(fmt,va)
#endif

#include <cstddef>
#include <cstdint>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

// Define endianness conversion routines
static inline uint16_t letohs(uint16_t x) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&x);
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static inline uint32_t letohl(uint32_t x) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&x);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline uint64_t letohll(uint64_t x) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&x);
    return ((uint64_t)b[0] <<  0) | ((uint64_t)b[1] <<  8) | ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40) | ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
}

static inline uint16_t betohs(uint16_t x) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&x);
    return (uint16_t)((uint16_t)b[1] | ((uint16_t)b[0] << 8));
}

static inline uint32_t betohl(uint32_t x) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&x);
    return (uint32_t)b[3] | ((uint32_t)b[2] << 8) | ((uint32_t)b[1] << 16) | ((uint32_t)b[0] << 24);
}

static inline uint64_t betohll(uint64_t x) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&x);
    return ((uint64_t)b[7] <<  0) | ((uint64_t)b[6] <<  8) | ((uint64_t)b[5] << 16) | ((uint64_t)b[4] << 24) |
           ((uint64_t)b[3] << 32) | ((uint64_t)b[2] << 40) | ((uint64_t)b[1] << 48) | ((uint64_t)b[0] << 56);
}

#endif

