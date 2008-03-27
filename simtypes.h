#ifndef SIMTYPES_H
#define SIMTYPES_H

#include "types.h"
#include <string>

namespace Simulator
{

typedef size_t   PID;           // Processor index
typedef size_t   TID;           // Thread index
typedef size_t   CID;           // Cache index
typedef size_t   PSize;         // Processor list size
typedef size_t   TSize;         // Thread list size
typedef uint64_t CycleNo;       // Cycle Number
typedef size_t   RegIndex;      // Index into a register file
typedef size_t   RegSize;       // Size of something in the register file
typedef uint64_t MemAddr;       // Address into memory
typedef uint64_t MemSize;       // Size of something in memory
typedef uint32_t Instruction;   // Instruction bits
typedef size_t   Place;			// Place identifier
typedef size_t   FSize;         // Family list size

#ifdef NDEBUG
typedef size_t   LFID;          // Local family index
typedef size_t   GFID;          // Group family index
#else
// In debug mode, make the GFID incompatible with LFID, so compilation
// will catch any LFID-to-GFID errors in the code.

// Note: strong typedef template based on Boost definition
#define STRONG_TYPEDEF(T, D)									\
struct D                                                        \
{                                                               \
    T t;                                                        \
    explicit D(const T t_) : t(t_) {};                          \
    D(){};                                                      \
    D(const D & t_) : t(t_.t){}                                 \
    D & operator=(const D & rhs) { t = rhs.t; return *this;}    \
    D & operator=(const T & rhs) { t = rhs; return *this;}      \
    operator const T & () const {return t; }                    \
    operator T & () { return t; }                               \
    bool operator==(const D & rhs) const { return t == rhs.t; } \
    bool operator<(const D & rhs) const { return t < rhs.t; }   \
};

STRONG_TYPEDEF(size_t, GFID);	// Group family index
STRONG_TYPEDEF(size_t, LFID);	// Local family index
#endif

typedef unsigned int RegType;
static const RegType RT_INTEGER    = 0;
static const RegType RT_FLOAT      = 1;
static const RegType NUM_REG_TYPES = 2;

static const CycleNo INFINITE_CYCLES = (CycleNo)-1;

enum SharedType
{
    ST_LOCAL,
    ST_FIRST,
    ST_LAST,
    ST_PARENT,
};

enum RegGroup
{
    RG_GLOBAL,
    RG_SHARED,
    RG_DEPENDENT,
    RG_LOCAL,
};

static RegIndex INVALID_REG_INDEX = (RegIndex)-1;

struct RegAddr
{
    RegType  type;
    RegIndex index;

    bool operator< (const RegAddr& addr) const { return (type < addr.type) || (type == addr.type && index < addr.index); }
    bool operator==(const RegAddr& addr) const { return type == addr.type && index == addr.index; }
    bool operator!=(const RegAddr& addr) const { return !(*this == addr); }
    
	bool valid()    const { return index != INVALID_REG_INDEX; }
    bool invalid()  const { return !valid(); }
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
    FST_IDLE,
    FST_ACTIVE,
    FST_KILLED
};

std::ostream& operator << (std::ostream& output, const RegAddr& reg);

static const PID        INVALID_PID  = (PID) -1;
static const LFID       INVALID_LFID = (LFID)-1;
static const GFID       INVALID_GFID = GFID(-1);
static const TID        INVALID_TID  = (TID) -1;
static const CID        INVALID_CID  = (CID) -1;
static const RegAddr    INVALID_REG  = MAKE_REGADDR(RT_INTEGER, INVALID_REG_INDEX);

struct MemTag
{
	LFID fid;
	CID  cid;
	bool data;

    MemTag& operator = (const MemTag& t) {
        if (this != &t) {
			fid  = t.fid;
            data = t.data;
			cid  = t.cid;
        }
        return *this;
    }

    MemTag(const MemTag& t) { *this = t; }
    MemTag(CID _cid, bool _data) : cid(_cid), data(_data) {}
	MemTag(LFID _fid) : fid(_fid) {}
    MemTag() {}
};

// This structure stores memory request information, to be used in RegValue.
struct MemRequest
{
	LFID		 fid;		// Family that made the request
	unsigned int offset;	// Offset in the cache-line, in bytes
	size_t       size;		// Size of data, in bytes
	RegAddr      next;		// Next register waiting on the cache-line
};

enum Result
{
    FAILED,
    DELAYED,
    SUCCESS
};

struct RemoteTID
{
    PID pid;    // Processor ID
    TID tid;    // Thread ID
};

enum RegState {
    RST_INVALID,
    RST_EMPTY,
	RST_PENDING,
    RST_WAITING,
    RST_FULL,
};

struct Float
{
	union {
		struct {
			unsigned long long fraction:52;
			unsigned long long exponent:11;
			unsigned long long sign:1;
		};
		uint64_t integer;
	};

	float  tofloat()  const;
	double todouble() const;
	void fromfloat (float  f);
	void fromdouble(double f);
};

class IComponent;

struct RegValue {
    RegState m_state;
    union {
        Float    m_float;
        uint64_t m_integer;
		struct
		{
			TID			m_tid;
			MemRequest	m_request;
			IComponent* m_component;	// To verify who is writing back
		};
    };
};

struct RemoteRegAddr
{
    GFID    fid;
    RegAddr reg;
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

#pragma pack(1)
struct RegsNo
{
    uint16_t globals:5;
    uint16_t shareds:5;
    uint16_t locals:5;
};
#pragma pack()

enum InstrFormat
{
    IFORMAT_MEM_LOAD,
    IFORMAT_MEM_STORE,
    IFORMAT_JUMP,
    IFORMAT_OP,
    IFORMAT_BRA,
    IFORMAT_PAL,
    IFORMAT_MISC,
	IFORMAT_SPECIAL,
    IFORMAT_INVALID
};

// Floating Point Control Register
typedef uint64_t FPCR;
static const FPCR FPCR_SUM      = 0x8000000000000000ULL;
static const FPCR FPCR_INED     = 0x4000000000000000ULL;
static const FPCR FPCR_UNFD     = 0x2000000000000000ULL;
static const FPCR FPCR_UNDZ     = 0x1000000000000000ULL;
static const FPCR FPCR_DYN_RM   = 0x0C00000000000000ULL;
static const FPCR FPCR_DYN_RM_C = 0x0000000000000000ULL; // Chopped
static const FPCR FPCR_DYN_RM_M = 0x0400000000000000ULL; // Minus
static const FPCR FPCR_DYN_RM_N = 0x0800000000000000ULL; // Normal
static const FPCR FPCR_DYN_RM_P = 0x0C00000000000000ULL; // Plus
static const FPCR FPCR_IOV      = 0x0200000000000000ULL;
static const FPCR FPCR_INE      = 0x0100000000000000ULL;
static const FPCR FPCR_UNF      = 0x0080000000000000ULL;
static const FPCR FPCR_OVF      = 0x0040000000000000ULL;
static const FPCR FPCR_DZE      = 0x0020000000000000ULL;
static const FPCR FPCR_INV      = 0x0010000000000000ULL;
static const FPCR FPCR_OVFD     = 0x0008000000000000ULL;
static const FPCR FPCR_DZED     = 0x0004000000000000ULL;
static const FPCR FPCR_INVD     = 0x0002000000000000ULL;
static const FPCR FPCR_DNZ      = 0x0001000000000000ULL;
static const FPCR FPCR_DNOD     = 0x0000800000000000ULL;

enum ExitCode
{
    EXIT_NORMAL = 0,
    EXIT_BREAK,
    EXIT_SQUEEZE,
    EXIT_KILL,
};

// Serialize and unserialize a register
void SerializeRegister(RegType type, const RegValue& value, const void* data, size_t size);
RegValue UnserializeRegister(RegType type, const void* data, size_t size);

// Unserialize an instruction
Instruction UnserializeInstruction(const void* data);
                                
}
#endif

