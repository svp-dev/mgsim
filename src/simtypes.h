#ifndef SIMTYPES_H
#define SIMTYPES_H

#include "types.h"
#include "Archures.h"
#include <string>

namespace Simulator
{

// Determine endianness of architecure
#define ARCH_LITTLE_ENDIAN 1
#define ARCH_BIG_ENDIAN    2

#if TARGET_ARCH == ARCH_ALPHA
#define ARCH_ENDIANNESS ARCH_LITTLE_ENDIAN
#else
#define ARCH_ENDIANNESS ARCH_BIG_ENDIAN
#endif

typedef size_t   GPID;          ///< Global (grid) processor index
typedef size_t   LPID;          ///< Local (group) Processor index
typedef size_t   TID;           ///< Thread index
typedef size_t   CID;           ///< Cache index
typedef size_t   PSize;         ///< Processor list size
typedef size_t   TSize;         ///< Thread list size
typedef uint64_t CycleNo;       ///< Cycle Number
typedef uint64_t Capability;    ///< Capability
typedef size_t   RegIndex;      ///< Index into a register file
typedef size_t   RegSize;       ///< Size of something in the register file
typedef size_t   FSize;         ///< Family list size
typedef size_t   LFID;          ///< Local family index
typedef size_t   GFID;          ///< Global family index

/// Place identifier
struct PlaceID
{
    GPID       pid;
    Capability capability;
    bool       exclusive;
    
    bool IsLocal() const { return pid == 0 && capability == 0; }
    bool IsDelegated(GPID self_pid) const { return !IsLocal() && self_pid != pid; }
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
        uint64_t integer;
    };

	double tofloat() const;
	void fromfloat(double f);
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
        uint64_t integer;
    };

	double tofloat() const;
	void fromfloat(double f);
};

#if TARGET_ARCH == ARCH_ALPHA
typedef uint64_t MemAddr;       ///< Address into memory
typedef uint64_t MemSize;       ///< Size of something in memory
typedef uint32_t Instruction;   ///< Instruction bits
typedef uint64_t Integer;       ///< Natural integer type
typedef int64_t  SInteger;      ///< Natural integer type, signed
typedef Float64  Float;         ///< Natural floating point type
#define MEMSIZE_MAX UINT64_MAX
#elif TARGET_ARCH == ARCH_SPARC
typedef uint32_t MemAddr;       ///< Address into memory
typedef uint32_t MemSize;       ///< Size of something in memory
typedef uint32_t Instruction;   ///< Instruction bits
typedef uint32_t Integer;       ///< Natural integer type
typedef int32_t  SInteger;      ///< Natural integer type, signed
typedef Float32  Float;         ///< Natural floating point type
#define MEMSIZE_MAX UINT32_MAX
#endif

/// An FP value that can be of different sizes
struct MultiFloat
{
    union
    {
        Float32 _32;
        Float64 _64;
    };

    uint64_t toint  (int size) const;
    double   tofloat(int size) const;
    void fromint(uint64_t i, int size);
    void fromfloat(double f, int size);
};

/// An integer value that can be of different sizes
struct MultiInteger
{
    union
    {
        uint32_t _32;
        uint64_t _64;
    };

    uint64_t get(int size) const;
    void set(uint64_t v, int size);
    MultiInteger& operator=(uint64_t v) { set(v, sizeof(Integer)); return *this; }
};

typedef unsigned int RegType;
static const RegType RT_INTEGER    = 0;
static const RegType RT_FLOAT      = 1;
static const RegType NUM_REG_TYPES = 2;

static const CycleNo INFINITE_CYCLES = (CycleNo)-1;

/// Different types of shared classes
enum SharedType
{
    ST_LOCAL,   ///< The shareds of the previous thread on the same CPU
    ST_FIRST,   ///< The dependents (copy) in the first thread in the block
    ST_LAST,    ///< The shareds in the last thread in the block
    ST_PARENT,  ///< The child-shareds in the parent thread
};

#pragma pack(1)
struct RegsNo
{
    uint16_t globals:5;
    uint16_t shareds:5;
    uint16_t locals:5;
};
#pragma pack()

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
extern uint8_t GetRegisterClass(uint8_t addr, const RegsNo& regs, RegClass* rc);

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
    TST_KILLED,
};

enum FamilyState
{
    FST_EMPTY,
	FST_ALLOCATED,
	FST_CREATE_QUEUED,
	FST_CREATING,
	FST_DELEGATED,
    FST_IDLE,
    FST_ACTIVE,
    FST_KILLED
};

std::ostream& operator << (std::ostream& output, const RegAddr& reg);

static const GPID    INVALID_GPID = GPID(-1);
static const LPID    INVALID_LPID = LPID(-1);
static const LFID    INVALID_LFID = LFID(-1);
static const GFID    INVALID_GFID = GFID(-1);
static const TID     INVALID_TID  = TID (-1);
static const CID     INVALID_CID  = CID (-1);
static const RegAddr INVALID_REG  = MAKE_REGADDR(RT_INTEGER, INVALID_REG_INDEX);

struct MemTag
{
    union
    {
        // For writes
        struct {
            LFID fid;
            TID  tid;
        };
    
        // For instruction or data reads
        struct {
            CID  cid;
            bool data;
        };
    };

    MemTag(CID _cid, bool _data) : cid(_cid), data(_data) {}
	MemTag(LFID _fid, TID _tid) : fid(_fid), tid(_tid) {}
    MemTag() {}
};

/// This structure stores memory request information, to be used in RegValue.
struct MemoryRequest
{
	LFID	 	 fid;		  ///< Family that made the request
	unsigned int offset;	  ///< Offset in the cache-line, in bytes
	size_t       size;		  ///< Size of data, in bytes
	bool         sign_extend; ///< Sign-extend the loaded value into the register?
	RegAddr      next;	 	  ///< Next register waiting on the cache-line
};

/// This structure stores remote request information for shareds and globals.
struct RemoteRequest
{
    GPID     pid;   ///< ID of the requesting CPU (INVALID for group requests)
    RegIndex reg;   ///< Destination register (INVALID for non-requests)
};

enum Result
{
    FAILED,
    DELAYED,
    SUCCESS
};

enum RegState {
    RST_INVALID,
    RST_EMPTY,
    RST_WAITING,
    RST_FULL,
};

struct ThreadQueue
{
    TID head;
    TID tail;
};

struct FamilyQueue
{
    LFID head;
    LFID tail;
};

class IComponent;

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
			RemoteRequest m_remote;     ///< Remote request information for shareds and globals.
		};
    };
};

/// This structure represents a remote request for a global or shared register
struct RemoteRegAddr
{
    RegAddr reg;    ///< The type and (logical) index of the register
    GFID    fid;    ///< The ID of the family containing the global or shared
};

enum ExitCode
{
    EXIT_NORMAL = 0,
    EXIT_BREAK,
    EXIT_SQUEEZE,
    EXIT_KILL,
};

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
#if TARGET_ARCH == ARCH_ALPHA
typedef uint64_t FPCR;  // Floating Point Control Register
#elif TARGET_ARCH == ARCH_SPARC
typedef uint32_t PSR;   // Processor State Register
typedef uint32_t FSR;   // Floating-Point State Register
#endif
                                
}
#endif

