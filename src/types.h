#ifndef TYPES_H
#define TYPES_H

// Support non-compliant C99 compilers (different long long type)
#if defined(_MSC_VER)
typedef __int8           int8_t;
typedef __int16          int16_t;
typedef __int32          int32_t;
typedef __int64          int64_t;
typedef unsigned __int8  uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;

static const int8_t   INT8_MIN  = -128;
static const int8_t   INT8_MAX  =  127;
static const int16_t  INT16_MIN = -32768;
static const int16_t  INT16_MAX =  32767;
static const int32_t  INT32_MIN = 0x80000000L;
static const int32_t  INT32_MAX = 0x7FFFFFFFL;
static const int64_t  INT64_MIN = 0x8000000000000000LL;
static const int64_t  INT64_MAX = 0x7FFFFFFFFFFFFFFFLL;
static const uint8_t  UINT8_MIN  = 0U;
static const uint8_t  UINT8_MAX  = 0xFFU;
static const uint16_t UINT16_MIN = 0U;
static const uint16_t UINT16_MAX = 0xFFFFU;
static const uint32_t UINT32_MIN = 0UL;
static const uint32_t UINT32_MAX = 0xFFFFFFFFUL;
static const uint64_t UINT64_MIN = 0ULL;
static const uint64_t UINT64_MAX = 0xFFFFFFFFFFFFFFFFULL;
#else
// We just hope that this compiler properly supports the C++ standard
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#endif

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

