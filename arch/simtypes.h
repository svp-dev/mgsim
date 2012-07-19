#ifndef SIMTYPES_H
#define SIMTYPES_H

#include "sim/types.h"
#include "Archures.h"
#include <string>
#include <cassert>

namespace Simulator
{

// Determine endianness of architecure
#define ARCH_LITTLE_ENDIAN 1
#define ARCH_BIG_ENDIAN    2

#if defined(TARGET_MTALPHA)
#define ARCH_ENDIANNESS ARCH_LITTLE_ENDIAN
#else
#define ARCH_ENDIANNESS ARCH_BIG_ENDIAN
#endif

typedef size_t   PID;           ///< Processor index
typedef size_t   TID;           ///< Thread index
typedef size_t   CID;           ///< Cache index
typedef size_t   PSize;         ///< Processor list size
typedef size_t   TSize;         ///< Thread list size
typedef size_t   RegIndex;      ///< Index into a register file
typedef size_t   RegSize;       ///< Size of something in the register file
typedef size_t   FSize;         ///< Family list size
typedef size_t   LFID;          ///< Local family index

typedef size_t   WClientID; ///< Entity ID to match memory writes (either TID or LFID depending on config)

enum ContextType
{
    CONTEXT_NORMAL = 0,
    CONTEXT_RESERVED,
    CONTEXT_EXCLUSIVE,
    NUM_CONTEXT_TYPES
};

/// 32-bit IEEE-754 float
struct Float32
{
    union {
        struct {
            unsigned long long fraction:23;
            unsigned long long exponent:8;
            unsigned long long sign:1;
        };
#ifndef EMULATE_IEEE754
        float   floating;
#endif
        uint32_t integer;
    };

#ifdef EMULATE_IEEE754
	double tofloat() const;
	void fromfloat(double f);
#else
	double tofloat() const { return floating; }	
	void fromfloat(double f) { floating = f; }
#endif
};

/// 64-bit IEEE-754 float
struct Float64
{
    union {
        struct {
            unsigned long long fraction:52;
            unsigned long long exponent:11;
            unsigned long long sign:1;
        };
#ifndef EMULATE_IEEE754
        double   floating;
#endif
        uint64_t integer;
    };

#ifdef EMULATE_IEEE754
	double tofloat() const;
	void fromfloat(double f);
#else
	double tofloat() const { return floating; }
	void fromfloat(double f) { floating = f; }
#endif
};

#if defined(TARGET_MTALPHA)
typedef uint64_t MemAddr;       ///< Address into memory
typedef uint64_t MemSize;       ///< Size of something in memory
typedef uint32_t Instruction;   ///< Instruction bits
typedef uint64_t Integer;       ///< Natural integer type
typedef int64_t  SInteger;      ///< Natural integer type, signed
typedef Float64  Float;         ///< Natural floating point type
#define MEMSIZE_MAX UINT64_MAX
#elif defined(TARGET_MTSPARC)
typedef uint32_t MemAddr;       ///< Address into memory
typedef uint32_t MemSize;       ///< Size of something in memory
typedef uint32_t Instruction;   ///< Instruction bits
typedef uint32_t Integer;       ///< Natural integer type
typedef int32_t  SInteger;      ///< Natural integer type, signed
typedef Float32  Float;         ///< Natural floating point type
#define MEMSIZE_MAX UINT32_MAX
#endif

typedef Integer  FCapability;   ///< Capability for a family
typedef Integer  PCapability;   ///< Capability for a place
typedef Integer  ProcessID;     ///< Process ID bits for MMU (virtual memory)

/// Place identifier
struct PlaceID
{
    PID         pid;
    PSize       size;
    PCapability capability;
    std::string str() const;
};

/// A globally unique family identifier
struct FID
{
    PID         pid;
    LFID        lfid;
    FCapability capability;
    std::string str() const;
};

/// Infor for bundle creation
struct Bundle
{
    MemAddr  pc;
    Integer  parameter;
    SInteger index;
}; 
 
/// Program-specified allocation type for a place allocation
enum AllocationType
{
    ALLOCATE_NORMAL = 0,    ///< Allocate over the entire place, or less
    ALLOCATE_EXACT,         ///< Allocate over the entire place exactly
    ALLOCATE_BALANCED,      ///< Allocate a single context on the best core in the place
    ALLOCATE_SINGLE,        ///< Allocate a single context on the first core in the place
};

/// An FP value that can be of different sizes
struct MultiFloat
{
    union
    {
        Float32 _32;
        Float64 _64;
    };

    uint64_t toint  (int size) const
    {
        switch (size)
        {
        case 4: return _32.integer;
        case 8: return _64.integer;
        }
        assert(false);
        return 0;
    }

    double   tofloat(int size) const
    {
        switch (size)
        {
        case 4: return _32.tofloat();
        case 8: return _64.tofloat();
        }
        assert(false);
        return 0.0f;
    }

    void fromint(uint64_t i, int size)
    {
        switch (size)
        {
        case 4: _32.integer = i; break;
        case 8: _64.integer = i; break;
        default: assert(0);
        }
    }
    
    void fromfloat(double f, int size)
    {
        switch (size)
        {
        case 4: _32.fromfloat(f); break;
        case 8: _64.fromfloat(f); break;
        default: assert(0);
        }
    }
};

/// An integer value that can be of different sizes
struct MultiInteger
{
    union
    {
        uint32_t _32;
        uint64_t _64;
    };

    uint64_t get(int size) const
    {
        switch (size)
        {
        case 4: return _32; break;
        case 8: return _64; break;
        default: assert(0);
        }
        return 0;
    }
    void set(uint64_t v, int size)
    {
        switch (size)
        {
        case 4: _32 = (uint32_t)v; break;
        case 8: _64 = v; break;
        default: assert(0);
        }
    }

    MultiInteger& operator=(uint64_t v) { set(v, sizeof(Integer)); return *this; }
};

/*
typedef unsigned int RegType;
static const RegType RT_INTEGER    = 0;
static const RegType RT_FLOAT      = 1;
static const RegType NUM_REG_TYPES = 2;
*/
enum RegType
{
    RT_INTEGER = 0,
    RT_FLOAT   = 1,
};
/* NUM_REG_TYPES is for "value" registers used in computations
   and communication */
static const RegType NUM_REG_TYPES = (RegType)(RT_FLOAT + 1);
/* NUM_PHY_REG_TYPES may include "special" registers used
   for synchronization */
static const RegType NUM_PHY_REG_TYPES = (RegType)(RT_FLOAT + 1);

// These fields only have to be 5 bits wide
struct RegsNo
{
    unsigned char globals;
    unsigned char shareds;
    unsigned char locals;
};

/// Register classes
enum RegClass
{
    RC_GLOBAL,      ///< Globals
    RC_SHARED,      ///< Shareds
    RC_LOCAL,       ///< Locals
    RC_DEPENDENT,   ///< Dependents
    RC_RAZ,         ///< Read-as-zero
};

// ISA-specific function to map virtual registers to register classes
extern unsigned char GetRegisterClass(unsigned char addr, const RegsNo& regs, RegClass* rc);

static RegIndex INVALID_REG_INDEX = (RegIndex)-1;

struct RegAddr
{
    RegType  type;
    RegIndex index;

    bool operator< (const RegAddr& addr) const { return (type < addr.type) || (type == addr.type && index < addr.index); }
    bool operator==(const RegAddr& addr) const { return type == addr.type && index == addr.index; }
    bool operator!=(const RegAddr& addr) const { return !(*this == addr); }
    
	bool valid() const { return index != INVALID_REG_INDEX; }
    std::string str() const;
};

static RegAddr MAKE_REGADDR(RegType type, RegIndex index)
{
	RegAddr addr;
	addr.type  = type;
	addr.index = index;
	return addr;
}

enum ThreadState
{
    TST_EMPTY,
    TST_WAITING,
    TST_READY,
    TST_ACTIVE,
    TST_RUNNING,
    TST_SUSPENDED,
    TST_UNUSED,
    TST_TERMINATED,
    TST_NUMSTATES
};

extern const char* const ThreadStateNames[TST_NUMSTATES];

enum FamilyState
{
    FST_EMPTY,
	FST_ALLOCATED,
	FST_CREATE_QUEUED,
	FST_CREATING,
    FST_ACTIVE,
    FST_TERMINATED,
};

enum ExitCode
{
    EXITCODE_NORMAL,    ///< Family terminated normally
    EXITCODE_KILLED,    ///< Family was terminated with a KILL
    EXITCODE_BROKEN,    ///< Family was terminated with a BREAK
    EXITCODE_NONE,      ///< Family hasn't terminated yet
};

std::ostream& operator << (std::ostream& output, const RegAddr& reg);

static const PID     INVALID_PID  = PID (-1);
static const LFID    INVALID_LFID = LFID(-1);
static const TID     INVALID_TID  = TID (-1);
static const WClientID INVALID_WCLIENTID = WClientID(-1);
static const CID     INVALID_CID  = CID (-1);
static const RegAddr INVALID_REG  = MAKE_REGADDR(RT_INTEGER, INVALID_REG_INDEX);

/// This structure stores memory request information, to be used in RegValue.
struct MemoryRequest
{
	LFID	 	 fid;		  ///< Family that made the request
	unsigned int offset;	  ///< Offset in the cache-line, in bytes
	size_t       size;		  ///< Size of data, in bytes
	bool         sign_extend; ///< Sign-extend the loaded value into the register?
	RegAddr      next;	 	  ///< Next register waiting on the cache-line
        std::string  str() const;
};

/// Different types of shared classes
enum RemoteRegType
{
    RRT_RAW,                ///< Plain register write, no family indirection
    RRT_GLOBAL,             ///< The globals
    RRT_FIRST_DEPENDENT,    ///< The dependents in the first thread in the family
    RRT_LAST_SHARED,        ///< The last shareds in the family, meant for the parent
    RRT_DETACH,             ///< Fake remote reg type. This signals family detachment
    RRT_BRK,                ///< Fake remote reg type. This signals family break
};

const char* GetRemoteRegisterTypeString(RemoteRegType type);

/// This structure represents a remote global or shared register
struct RemoteRegAddr
{
    RemoteRegType type; ///< The type of register
    FID           fid;  ///< The global FID of the family
    RegAddr       reg;  ///< The type and (logical) index of the register
};

enum FamilyProperty {
    FAMPROP_START,
    FAMPROP_LIMIT,
    FAMPROP_STEP,
    FAMPROP_BLOCK,
};

enum RegState {
    RST_INVALID,        ///< The register has no valid value (not used in Register File)
    RST_EMPTY,          ///< The register is empty
    RST_PENDING,        ///< The register is empty but will be written later
    RST_WAITING,        ///< The register is empty and threads are waiting on it
    RST_FULL,           ///< The register is full
};

struct ThreadQueue
{
    TID head;
    TID tail;
    std::string str() const;
};

struct FamilyQueue
{
    LFID head;
    LFID tail;
};

struct RegValue
{
    RegState m_state;       ///< State of the register.
    union
    {
        Float   m_float;    ///< Value of the register, if it is an FP register.
        Integer m_integer;  ///< Value of the register, if it is an integer register.
        
		struct
		{
		    ThreadQueue   m_waiting;    ///< List of the threads that are waiting on the register.
			MemoryRequest m_memory;     ///< Memory request information for pending registers.
		};
    };
    std::string str(RegType t) const;
};

static inline RegValue MAKE_EMPTY_REG()
{
    RegValue value;
    value.m_state        = RST_EMPTY;
    value.m_waiting.head = INVALID_TID;
    value.m_memory.size  = 0;
    return value;
}

/**
 * @brief Serialize a register
 * Serializes a register by writing the integer value to memory in an
 * architecture-dependent encoding.
 * @param type type of the data.
 * @param value the value to serialize.
 * @param data pointer to a block of memory that will receive the serialized register.
 * @param size size of the value to serialize, in bytes.
 */
void SerializeRegister(RegType type, uint64_t value, void* data, size_t size);

/**
 * @brief Unserialize a register
 * Unserializes a register by reading the integer value from memory with an
 * architecture-dependent decoding.
 * @param type type of the data.
 * @param data pointer to a block of memory that contains the serialized register.
 * @param size size of the value to unserialize, in bytes.
 * @return the unserialized value.
 */
uint64_t UnserializeRegister(RegType type, const void* data, size_t size);

/**
 * @brief Unserialize an instruction
 * Unserializes an instruction by reading the integer value from memory with an
 * architecture-dependent decoding.
 * @param data pointer to a block of memory that contains the serialized instruction.
 * @return the unserialized instruction.
 */
Instruction UnserializeInstruction(const void* data);

//
// Architecture specific global types
//
#if defined(TARGET_MTALPHA)
typedef uint64_t FPCR;  // Floating Point Control Register
#elif defined(TARGET_MTSPARC)
typedef uint32_t PSR;   // Processor State Register
typedef uint32_t FSR;   // Floating-Point State Register
#endif

}
#endif

