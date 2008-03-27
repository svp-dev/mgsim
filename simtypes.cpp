#include "simtypes.h"
#include <iomanip>
#include <cassert>
#include <sstream>
#include <cmath>
#include <limits>
using namespace std;

namespace Simulator
{

string RegAddr::str() const
{
    if (this->invalid())
    {
        return "N/A  ";
    }
    stringstream ss;
    ss << (type == RT_INTEGER ? 'R' : 'F') << hex << uppercase << setw(4) << setfill('0') << index;
    return ss.str();
}

ostream& operator << (ostream& output, const RegAddr& reg)
{
    output << reg.str();
    return output;
}

static uint64_t LittleEndianToInteger(const void* data, size_t size)
{
    uint64_t value = 0;
    while (size > 0)
    {
        value = (value << 8) + *((uint8_t*)data + --size);
    }
    return value;
}

static void IntegerToLittleEndian(uint64_t value, const void* data, size_t size)
{
    for (size_t i = 0; i < size; ++i, value >>= 8)
    {
        *((uint8_t*)data + i) = (uint8_t)(value & 0xFF);
    }
}

// This function maps an 8-bit exponent to an 11-bit exponent, as described
// in the Alpha Handbook, section 2.2.6.1.
static uint64_t MAP_S(uint64_t exp)
{
	if (exp == 0xFF) return 0x7FF;
	if (exp == 0x00) return 0x000;
	if (exp &  0x80) return 0x400 | (exp & 0x7F);
	return 0x380 | (exp & 0x7F);
}

static Float LittleEndianToFloat(const void* data, size_t size)
{
	assert(size == 4 || size == 8);
	uint64_t d = LittleEndianToInteger(data, size);
	Float f;
	if (size == 4) {
		f.sign     = (d >> 31) & 1;
		f.exponent = MAP_S((d >> 23) & 0xFF);
		f.fraction = (d & 0x7FFFFF) << 29;
	} else {
		f.sign     = (d >> 63) & 1;
		f.exponent = (d >> 52) & 0x7FF;
		f.fraction = (d & 0xFFFFFFFFFFFFFULL);
	}
	return f;
}

static void FloatToLittleEndian(const Float& value, const void* data, size_t size)
{
	assert(size == 4 || size == 8);
	if (size == 4) {
		IntegerToLittleEndian(
			(( value.sign            &        1) << 31) |
			(((value.exponent >> 10) &        1) << 30) |
			(((value.exponent >>  0) &     0x7F) << 22) |
			(((value.fraction >> 29) & 0x7FFFFF) <<  0)
			, data, size);
	} else {
		IntegerToLittleEndian(
			((unsigned long long)value.sign     << 63) |
			((unsigned long long)value.exponent << 52) |
			((unsigned long long)value.fraction <<  0),
			data, size);
	}
}

RegValue UnserializeRegister(RegType type, const void* data, size_t size)
{
    RegValue value;
    value.m_state = RST_FULL;
    switch (type)
    {
		case RT_INTEGER:
			value.m_integer = LittleEndianToInteger(data, size);
			if (size == 4)
			{
				// Sign-extend
				value.m_integer = (int64_t)(int32_t)(uint32_t)value.m_integer;
			}
			break;

        case RT_FLOAT:
			value.m_float = LittleEndianToFloat(data, size);
			break;
    }
    return value;
}

Instruction UnserializeInstruction(const void* _data)
{
    Instruction i = 0;
    const uint8_t* data = (uint8_t*)_data;
    for (int j = 3; j >= 0; j--)
    {
        i = (i << 8) | data[j];
    }
    return i;
}

void SerializeRegister(RegType type, const RegValue& value, const void* data, size_t size)
{
    switch (type)
    {
        case RT_INTEGER: IntegerToLittleEndian(value.m_integer, data, size); break;
        case RT_FLOAT:   FloatToLittleEndian  (value.m_float,   data, size); break;
    }
}

static const int          IEEE754_EXPONENT_BIAS = 1023;
static const unsigned int IEEE754_MAX_EXPONENT  = 2047;

float Float::tofloat() const
{
	float value = 0.0f;	// Default to zero
	if (exponent == IEEE754_MAX_EXPONENT)
	{
		value = (fraction == 0)
			? numeric_limits<float>::infinity()	// Infinity
			: (fraction & 0x8000000000000ULL)
				? numeric_limits<float>::quiet_NaN()	  // Not-a-Number (Quiet)
				: numeric_limits<float>::signaling_NaN(); // Not-a-Number (Signaling)
	}
	else 
	{
		// (De)normalized number
		unsigned long long f = fraction;
		for (int i = 0; i < 52; i++, f >>= 1)
		{
			if (f & 1) {
				value = value + 1;
			}
			value = value / 2;
		}

		if (exponent != 0)
		{
			// Normalized number
			value = ldexpf(value + 1, (int)exponent - IEEE754_EXPONENT_BIAS);
		}
	}
	return sign ? -value : value;
}

double Float::todouble() const
{
	double value = 0.0; // Default to zero
	if (exponent == IEEE754_MAX_EXPONENT)
	{
		value = (fraction == 0)
			? numeric_limits<double>::infinity()	// Infinity
			: (fraction & 0x8000000000000ULL)
				? numeric_limits<double>::quiet_NaN()	   // Not-a-Number (Quiet)
				: numeric_limits<double>::signaling_NaN(); // Not-a-Number (Signaling)
	}
	else 
	{
		// (De)normalized number
		unsigned long long f = fraction;
		for (int i = 0; i < 52; i++, f >>= 1)
		{
			if (f & 1) {
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

void Float::fromfloat(float f)
{
	if (f == numeric_limits<float>::infinity()) {
		// Positive infinity
		sign     = 0;
		fraction = 0;
		exponent = IEEE754_MAX_EXPONENT;
	}
	else if (f == -numeric_limits<float>::infinity()) {
		// Negative infinity
		sign     = 1;
		fraction = 0;
		exponent = IEEE754_MAX_EXPONENT;
	}
	else if (f != f) {
		// Not a Number (Quiet)
		sign     = 0;
		fraction = 0x4000000000000ULL;
		exponent = IEEE754_MAX_EXPONENT;
	}
	else
	{
		// (De)normalized number
		sign = (f < 0) ? 1 : 0;
		f    = fabs(f);
		
		int exp;
		f = frexp(f, &exp);
		
		fraction = 0;		
		if (exp != 0 || f != 0)
		{
			// Non-zero number
			if (f < 1) {
				exp--;
				f = f * 2;
			}

			for (int i = 0; i < 52; i++)
			{
				if (f >= 1) {
					fraction |= 1;
					f -= 1;
				}
				f = (f * 2);
				fraction <<= 1;
			}
			exp += IEEE754_EXPONENT_BIAS;
		}
		exponent = exp;
	}
}

void Float::fromdouble(double f)
{
	if (f == numeric_limits<double>::infinity()) {
		// Positive infinity
		sign     = 0;
		fraction = 0;
		exponent = IEEE754_MAX_EXPONENT;
	}
	else if (f == -numeric_limits<double>::infinity()) {
		// Negative infinity
		sign     = 1;
		fraction = 0;
		exponent = IEEE754_MAX_EXPONENT;
	}
	else if (f != f) {
		// Not a Number (Quiet)
		sign     = 0;
		fraction = 0x4000000000000ULL;
		exponent = IEEE754_MAX_EXPONENT;
	}
	else
	{
		// (De)normalized number
		sign = (f < 0) ? 1 : 0;
		f    = fabs(f);
		
		int exp;
		f = frexp(f, &exp);
		
		fraction = 0;		
		if (exp != 0 || f != 0)
		{
			// Non-zero number
			if (f < 1) {
				exp--;
				f = f * 2;
			}

			for (int i = 0; i < 52; i++)
			{
				if (f >= 1) {
					fraction |= 1;
					f -= 1;
				}
				f = (f * 2);
				fraction <<= 1;
			}
			exp += IEEE754_EXPONENT_BIAS;
		}
		exponent = exp;
	}
}

}

