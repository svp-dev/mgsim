#include "simtypes.h"
#include <iomanip>
#include <cassert>
#include <sstream>
#include <cmath>
#include <limits>
using namespace std;

namespace Simulator
{

const char* const ThreadStateNames[TST_NUMSTATES] = {
    "", "WAITING", "READY", "ACTIVE", "RUNNING", "SUSPENDED", "UNUSED", "TERMINATED"
};

string PlaceID::str() const
{
    ostringstream ss;
    ss << "P(" << hex << capability << ':' << dec << pid << '/' << size << ')';
    return ss.str();
}

string FID::str() const
{
    ostringstream ss;
    ss << "F(" << hex << capability << ":CPU" << dec << pid << "/F" << lfid << ')';
    return ss.str();
}

const char* GetRemoteRegisterTypeString(RemoteRegType type)
{
    switch (type)
    {
    case RRT_RAW:             return "raw";
    case RRT_GLOBAL:          return "global";
    case RRT_FIRST_DEPENDENT: return "first shared";
    case RRT_LAST_SHARED:     return "last shared";
    default:
        UNREACHABLE;
    }
}

string RegAddr::str() const
{
    if (valid())
    {
        stringstream ss;
        switch (type)
        {
        case RT_INTEGER: ss << 'R'; break;
        case RT_FLOAT:   ss << 'F'; break;
        }
        ss << hex << uppercase << setw(4) << setfill('0') << index;
        return ss.str();
    }
    return "N/A  ";
}

string ThreadQueue::str() const
{
    stringstream ss;

    ss << "Q(T" << head << ",T" << tail << ')';

    return ss.str();
}


string MemoryRequest::str() const
{
    stringstream ss;

    if (size == 0)
        ss << "NoMem";
    else
        ss << "Mem:(F" << fid
           << ',' << offset
           << ',' << size
           << ',' << sign_extend
           << ',' << next.str()
           << ')';

    return ss.str();
}

string RegValue::str(RegType type) const
{
    // Also see Pipeline::PipeValue::str()
    switch (m_state)
    {
    case RST_INVALID: return "INVALID";
    case RST_EMPTY:   return "[E]";
    case RST_PENDING: return "[P:" + m_memory.str() + "]";
    case RST_WAITING: return "[W:" + m_memory.str() + "," + m_waiting.str() + "]";
    case RST_FULL:{
        stringstream ss;
        ss << "[F:" << setw(sizeof(Integer) * 2) << setfill('0') << hex;
        if (type == RT_FLOAT)
            ss << m_float.integer << "] " << dec << m_float.floating;
        else
            ss << m_integer << "] " << dec << m_integer;
        return ss.str();
    }
    }
    UNREACHABLE;
}

ostream& operator << (ostream& output, const RegAddr& reg)
{
    output << reg.str();
    return output;
}

//
// Integer marshalling
//
#if ARCH_ENDIANNESS == ARCH_BIG_ENDIAN
static uint64_t BigEndianToInteger(const void* data, size_t size)
{
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i)
    {
        value = (value << 8) + *(static_cast<const uint8_t*>(data) + i);
    }
    return value;
}

static void IntegerToBigEndian(uint64_t value, void* data, size_t size)
{
    while (size-- > 0)
    {
        *(static_cast<uint8_t*>(data) + size) = static_cast<uint8_t>(value & 0xFF);
        value >>= 8;
    }
}
#else
static uint64_t LittleEndianToInteger(const void* data, size_t size)
{
    uint64_t value = 0;
    while (size-- > 0)
    {
        value = (value << 8) + *(static_cast<const uint8_t*>(data) + size);
    }
    return value;
}

static void IntegerToLittleEndian(uint64_t value, void* data, size_t size)
{
    for (size_t i = 0; i < size; ++i, value >>= 8)
    {
        *(static_cast<uint8_t*>(data) + i) = static_cast<uint8_t>(value & 0xFF);
    }
}
#endif

// Define shortcuts
#if ARCH_ENDIANNESS == ARCH_BIG_ENDIAN
static void     IntegerToEndian(uint64_t v, void* d, size_t s) { return IntegerToBigEndian(v,d,s); }
static uint64_t EndianToInteger(const void* d, size_t s)       { return BigEndianToInteger(d,s); }
#else
static void     IntegerToEndian(uint64_t v, void* d, size_t s) { return IntegerToLittleEndian(v,d,s); }
static uint64_t EndianToInteger(const void* d, size_t s)       { return LittleEndianToInteger(d,s); }
#endif

Instruction UnserializeInstruction(const void* _data)
{
    Instruction i = 0;
    const uint8_t* data = static_cast<const uint8_t*>(_data);
    #if ARCH_ENDIANNESS == ARCH_BIG_ENDIAN
    for (int j = 0; j <= 3; ++j) {
        i = (i << 8) | data[j];
    }
    #else
    for (int j = 3; j >= 0; j--) {
        i = (i << 8) | data[j];
    }
    #endif
    return i;
}

#if defined(TARGET_MTALPHA)
// This function maps an 8-bit exponent to an 11-bit exponent, as described
// in the Alpha Handbook, section 2.2.6.1.
static uint64_t MAP_S(uint64_t exp)
{
    if (exp == 0xFF) return 0x7FF;
    if (exp == 0x00) return 0x000;
    if (exp &  0x80) return 0x400 | (exp & 0x7F);
    return 0x380 | (exp & 0x7F);
}
#endif

uint64_t UnserializeRegister(RegType type, const void* data, size_t size)
{
    switch (type)
    {
        case RT_INTEGER:
        {
            uint64_t value = EndianToInteger(data, size);
#if defined(TARGET_MTALPHA) || defined(TARGET_MIPS32) || defined(TARGET_MIPS32EL) || defined(TARGET_OR1K)
            // The Alpha, OR1K and MIPS sign-extends 32-bits to 64-bits
            if (size == 4) {
                // Sign-extend
                value = (int64_t)(int32_t)(uint32_t)value;
            }
#endif
            return value;
        }

        case RT_FLOAT:
        {
            uint64_t value = EndianToInteger(data, size);
#if defined(TARGET_MTALPHA)
            // The Alpha returns 32-bit S_Floatings as 64-bit T_Floatings
            // See Alpha Manual, 4.8.3, Load S_Floating
            if (size == 4) {
                value = (((value >> 31) & 1) << 63) |         // Sign
                        (MAP_S((value >> 23) & 0xFF) << 52) | // Exponent
                        (value & 0x7FFFFF) << 29;             // Fraction
            }
#endif
            return value;
        }
    }
    return 0;
}

void SerializeRegister(RegType type, uint64_t value, void* data, size_t size)
{
    switch (type)
    {
        case RT_FLOAT:
#if defined(TARGET_MTALPHA)
            // The Alpha stores 32-bit S_Floatings internally as 64-bit T_Floatings
            // See Alpha Manual, 4.8.7, Store S_Floating
            if (size == 4)
            {
                value = (((value >> 62) &          3) << 30) |
                        (((value >> 29) & 0x3FFFFFFF) <<  0);
            }
#endif
        case RT_INTEGER:
            IntegerToEndian(value, data, size);
            break;
    }
}

#ifdef EMULATE_IEEE754

/**
 * @brief Class for manipulating IEEE-754 values.
 * This class implements function to convert variably-sized IEEE-754 variables
 * into doubles. The raw IEEE-754 data should be stored in an (array of)
 * integers.
 *
 * @tparam T    type of the integer that stores the data.
 * @tparam Exp  number of bits in the exponent.
 * @tparam Frac number of bits in the fraction.
 */
template <typename T, int Exp, int Frac>
class IEEE754
{
    static const int          IEEE754_EXPONENT_BIAS = (1  << (Exp - 1)) - 1;
    static const unsigned int IEEE754_MAX_EXPONENT  = (1U << (Exp - 0)) - 1;

    // Reads bits [start, start + num) from the "bigint" at data
    static unsigned long long GET_BITS(const T* data, unsigned int start, unsigned int num)
    {
        // Ensure we can store the result
        assert(num <= sizeof(unsigned long long) * 8);

        const unsigned int width = sizeof(T) * 8;
        if (start + num <= width)
        {
            const T mask = (1ULL << num) - 1;
            return (*data >> start) & mask;
        }
        else
        {
            // Move to the first block
            data += start / width;
            start = start % width;

            unsigned long long x = 0; // Return value
            unsigned int pos = 0;     // Bit-offset to put new bits
            while (num > 0)           // While we have bits to read
            {
                // Get number of bits in this block
                unsigned int n = min(width - start, num);
                T mask = ((n < width) ? (1ULL << n) : 0) - 1;

                // Read block, shift to the bits we want, mask them, move them to final position
                x |= ((*data >> start) & mask) << pos;

                num  -= n;  // We have n bits less to read
                pos  += n;  // New bits are n bits further in the result
                start = 0;  // New bits come from the start of the block
                data++;     // Moving to next block
            }
            return x;
        }
    }

    // Copies the value x into the bits [start, start + num)
    static void SET_BITS(T* data, unsigned int start, unsigned int num, unsigned long long x)
    {
        // Ensure we can store the value
        assert(num <= sizeof(unsigned long long) * 8);

        const unsigned int width = sizeof(T) * 8;
        if (start + num <= width)
        {
            const T mask = (1ULL << num) - 1;
            *data = (*data & ~(mask << start)) | ((x & mask) << start);
        }
        else
        {
            // Move to first block
            data += start / width;
            start = start % width;

            while (num > 0) // While we have bits to set
            {
                // Get number of bits in this block
                unsigned int n = min(width - start, num);
                T mask = ((n < width) ? (1ULL << n) : 0) - 1;

                // Read current data, clear to-be-written area, overwrite with new bits
                *data = (*data & ~(mask << start)) | ((x & mask) << start);

                num  -= n;  // We have n bits less to set
                x   >>= n;  // Shift out the bits we set
                start = 0;  // Next bits go to the start of the block
                data++;     // Moving to next block
            }
        }
    }

    // Tests if all the bits [start, start + num) are zero
    static bool IS_ZERO_BITS(const T* data, unsigned int start, unsigned int num)
    {
        const unsigned int width = sizeof(T) * 8;
        if (start + num <= width)
        {
            T mask = (1ULL << num) - 1;
            return ((*data >> start) & mask) == 0;
        }
        else
        {
            // Move to the first block
            data += start / width;
            start = start % width;

            while (num > 0) // While we have bits to check
            {
                // Get number of bits in this block
                unsigned int n = min(width - start, num);
                T mask = ((n < width) ? (1ULL << n) : 0) - 1;

                // Read block; shift to the bits we want, mask them, test them
                if (((*data >> start) & mask) != 0) {
                    return false;
                }

                num  -= n; // We have n bits less to check
                start = 0; // Next bits come from the start of the block
                data++;    // Moving to next block
            }
            return true;
        }
    }

    // Clears the bits [start, start + num)
    static void CLEAR_BITS(T* data, unsigned int start, unsigned int num)
    {
        const unsigned int width = sizeof(T) * 8;
        if (start + num <= width)
        {
            T mask = (1ULL << num) - 1;
            *data &= ~(mask << start);
        }
        else
        {
            // Move to the first block
            data += start / width;
            start = start % width;

            while (num > 0) // While we have bits to clear
            {
                // Get number of bits in this block
                unsigned int n = min(width - start, num);
                T mask = ((n < width) ? (1ULL << n) : 0) - 1;

                // Clear block
                *data &= ~(mask << start);

                num  -= n; // We have n bits less to clear
                start = 0; // Next bits come from the start of the block
                data++;    // Moving to next block
            }
        }
    }

public:
    static double tofloat(const T* data)
    {
        double value = 0.0; // Default to zero
        unsigned long long sign     = GET_BITS(data, Frac + Exp, 1);
        unsigned long long exponent = GET_BITS(data, Frac, Exp);

        if (exponent == IEEE754_MAX_EXPONENT)
        {
            value = (IS_ZERO_BITS(data, 0, Frac))
                ? numeric_limits<double>::infinity()    // Infinity
                : GET_BITS(data, Frac - 1, 1)           // Test highest fraction bit
                    ? numeric_limits<double>::quiet_NaN()      // Not-a-Number (Quiet)
                    : numeric_limits<double>::signaling_NaN(); // Not-a-Number (Signaling)
        }
        else
        {
            // (De)normalized number
            for (int i = 0; i < Frac; ++i)
            {
                if (GET_BITS(data, i, 1)) {
                    value = value + 1;
                }
                value = value / 2;
            }

            int exp = (int)exponent - IEEE754_EXPONENT_BIAS;
            if (exponent != 0) {
                // Normalized number
                value += 1;
            } else {
                // Denormalized number
                exp++;
            }
            value = ldexp(value, exp);
        }
        return sign ? -value : value;
    }

    static void fromfloat(T* data, double f)
    {
        CLEAR_BITS(data, 0, Frac);
        if (f == numeric_limits<double>::infinity()) {
            // Positive infinity
            SET_BITS(data, Frac + Exp, 1, 0);
            SET_BITS(data, Frac, Exp, IEEE754_MAX_EXPONENT);
        }
        else if (f == -numeric_limits<double>::infinity()) {
            // Negative infinity
            SET_BITS(data, Frac + Exp, 1, 1);
            SET_BITS(data, Frac, Exp, IEEE754_MAX_EXPONENT);
        }
        else if (f != f) {
            // Not a Number (Quiet)
            SET_BITS(data, Frac + Exp, 1, 0);
            SET_BITS(data, Frac, Exp, IEEE754_MAX_EXPONENT);
            SET_BITS(data, Frac - 1, 1, 1);
        }
        else
        {
            // (De)normalized number
            SET_BITS(data, Frac + Exp, 1, (f < 0) ? 1 : 0);
            f = fabs(f);

            int exp;
            f = frexp(f, &exp);

            if (exp != 0 || f != 0)
            {
                // Non-zero number
                if (f < 1) {
                    exp--;
                    f = f * 2;
                }

                if (f >= 1) {
                    f -= 1;
                }

                for (int i = Frac - 1; i >= 0; i--)
                {
                    f = (f * 2);
                    if (f >= 1) {
                        SET_BITS(data, i, 1, 1);
                        f -= 1;
                    }
                }
                exp += IEEE754_EXPONENT_BIAS;
            }
            SET_BITS(data, Frac, Exp, exp);
        }
    }
};

double Float32 ::tofloat() const     { return IEEE754<uint32_t,  8, 23>::tofloat(&integer); }
double Float64 ::tofloat() const     { return IEEE754<uint64_t, 11, 52>::tofloat(&integer); }
void   Float32 ::fromfloat(double f) { return IEEE754<uint32_t,  8, 23>::fromfloat(&integer, f); }
void   Float64 ::fromfloat(double f) { return IEEE754<uint64_t, 11, 52>::fromfloat(&integer, f); }

#endif // EMULATE_IEEE754

}

