// -*- c++ -*-
#ifndef ELF_H
#define ELF_H

#include <sim/types.h>

// Some ELF types
typedef uint32_t Elf32_Addr;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sxword;
typedef uint32_t Elf32_Xword;

typedef uint64_t Elf64_Addr;
typedef uint16_t Elf64_Half;
typedef uint64_t Elf64_Off;
typedef int32_t  Elf64_Sword;
typedef int64_t  Elf64_Sxword;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

static const int EI_NIDENT  = 16;
static const int EI_MAG0    = 0; // File identification
static const int EI_MAG1    = 1; // File identification
static const int EI_MAG2    = 2; // File identification
static const int EI_MAG3    = 3; // File identification
static const int EI_CLASS   = 4; // File class
static const int EI_DATA    = 5; // Data encoding
static const int EI_VERSION = 6; // File version
static const int EI_PAD     = 7; // Start of padding bytes

static const unsigned char ELFMAG0 = 0x7F; // e_ident[EI_MAG0]
static const unsigned char ELFMAG1 = 'E';  // e_ident[EI_MAG1]
static const unsigned char ELFMAG2 = 'L';  // e_ident[EI_MAG2]
static const unsigned char ELFMAG3 = 'F';  // e_ident[EI_MAG3]

// e_ident[EI_CLASS]
#define ELFCLASSNONE 0 // Invalid class
#define ELFCLASS32   1 // 32-bit objects
#define ELFCLASS64   2 // 64-bit objects

// e_ident[EI_DATA]
#define ELFDATANONE 0 // Invalid data encoding
#define ELFDATA2LSB 1 // 2's complement values, with the LSB occupying the lowest address.
#define ELFDATA2MSB 2 // 2's complement values, with the MSB occupying the lowest address.

// e_ident[EI_VERSION]
static const unsigned char EV_NONE    = 0; // Invalid version
static const unsigned char EV_CURRENT = 1; // Current version

#if defined(TARGET_MTALPHA)
#define ELFCLASS ELFCLASS64
#define ELFDATA  ELFDATA2LSB
#elif defined(TARGET_MTSPARC)
#define ELFCLASS ELFCLASS32
#define ELFDATA  ELFDATA2MSB
#elif defined(TARGET_MIPS32EL)
#define ELFCLASS ELFCLASS32
#define ELFDATA  ELFDATA2LSB
#elif defined(TARGET_MIPS32) || defined(TARGET_OR1K)
#define ELFCLASS ELFCLASS32
#define ELFDATA  ELFDATA2MSB
#endif

#if ELFDATA == ELFDATA2MSB
static inline uint64_t elftohll(uint64_t x) { return betohll(x); }
static inline uint32_t elftohl(uint32_t x)  { return betohl(x); }
static inline uint16_t elftohs(uint16_t x)  { return betohs(x); }
#else
static inline uint64_t elftohll(uint64_t x) { return letohll(x); }
static inline uint32_t elftohl(uint32_t x)  { return letohl(x); }
static inline uint16_t elftohs(uint16_t x)  { return letohs(x); }
#endif

#if ELFCLASS == ELFCLASS64
typedef Elf64_Addr   Elf_Addr;
typedef Elf64_Half   Elf_Half;
typedef Elf64_Off    Elf_Off;
typedef Elf64_Sword  Elf_Sword;
typedef Elf64_Word   Elf_Word;
typedef Elf64_Sxword Elf_Sxword;
typedef Elf64_Xword  Elf_Xword;

static inline Elf_Addr   elftoha  (Elf_Addr   x) { return elftohll(x); }
static inline Elf_Half   elftohh  (Elf_Half   x) { return elftohs(x); }
static inline Elf_Off    elftoho  (Elf_Off    x) { return elftohll(x); }
static inline Elf_Sword  elftohsw (Elf_Sword  x) { return elftohl(x); }
static inline Elf_Word   elftohw  (Elf_Word   x) { return elftohl(x); }
static inline Elf_Sxword elftohsxw(Elf_Sxword x) { return elftohll(x); }
static inline Elf_Xword  elftohxw (Elf_Xword  x) { return elftohll(x); }

#else
typedef Elf32_Addr   Elf_Addr;
typedef Elf32_Half   Elf_Half;
typedef Elf32_Off    Elf_Off;
typedef Elf32_Sword  Elf_Sword;
typedef Elf32_Word   Elf_Word;
typedef Elf32_Sxword Elf_Sxword;
typedef Elf32_Xword  Elf_Xword;

static inline Elf_Addr   elftoha  (Elf_Addr   x) { return elftohl(x); }
static inline Elf_Half   elftohh  (Elf_Half   x) { return elftohs(x); }
static inline Elf_Off    elftoho  (Elf_Off    x) { return elftohl(x); }
static inline Elf_Sword  elftohsw (Elf_Sword  x) { return elftohl(x); }
static inline Elf_Word   elftohw  (Elf_Word   x) { return elftohl(x); }
static inline Elf_Sxword elftohsxw(Elf_Sxword x) { return elftohl(x); }
static inline Elf_Xword  elftohxw (Elf_Xword  x) { return elftohl(x); }
#endif

static const Elf_Half ET_NONE   = 0; // No file type
static const Elf_Half ET_REL    = 1; // Relocatable file
static const Elf_Half ET_EXEC   = 2; // Executable file
static const Elf_Half ET_DYN    = 3; // Shared object file
static const Elf_Half ET_CORE   = 4; // Core file
static const Elf_Half ET_LOPROC = 0xff00; // Processor-specific
static const Elf_Half ET_HIPROC = 0xffff; // Processor-specific

// Legal values for e_machine (architecture)
static const Elf_Half EM_NONE        =  0; // No machine
static const Elf_Half EM_M32         =  1; // AT&T WE 32100
static const Elf_Half EM_SPARC       =  2; // SPARC
static const Elf_Half EM_386         =  3; // Intel 80386
static const Elf_Half EM_68K         =  4; // Motorola 68000
static const Elf_Half EM_88K         =  5; // Motorola 88000
static const Elf_Half EM_860         =  7; // Intel 80860
static const Elf_Half EM_MIPS        =  8; // MIPS R3000 big-endian
static const Elf_Half EM_S370        =  9; // Amdahl
static const Elf_Half EM_MIPS_RS4_BE = 10; // MIPS R4000 big-endian
static const Elf_Half EM_RS6000      = 11; // RS6000

static const Elf_Half EM_PARISC      = 15; // HPPA
static const Elf_Half EM_nCUBE       = 16; // nCUBE
static const Elf_Half EM_VPP500      = 17; // Fujitsu VPP500
static const Elf_Half EM_SPARC32PLUS = 18; // Sun's "v8plus"
static const Elf_Half EM_960         = 19; // Intel 80960
static const Elf_Half EM_PPC         = 20; // PowerPC

static const Elf_Half EM_V800        = 36; // NEC V800 series
static const Elf_Half EM_FR20        = 37; // Fujitsu FR20
static const Elf_Half EM_RH32        = 38; // TRW RH32
static const Elf_Half EM_MMA         = 39; // Fujitsu MMA
static const Elf_Half EM_ARM         = 40; // ARM
static const Elf_Half EM_FAKE_ALPHA  = 41; // Digital Alpha
static const Elf_Half EM_SH          = 42; // Hitachi SH
static const Elf_Half EM_SPARCV9     = 43; // SPARC v9 64-bit
static const Elf_Half EM_TRICORE     = 44; // Siemens Tricore
static const Elf_Half EM_ARC         = 45; // Argonaut RISC Core
static const Elf_Half EM_H8_300      = 46; // Hitachi H8/300
static const Elf_Half EM_H8_300H     = 47; // Hitachi H8/300H
static const Elf_Half EM_H8S         = 48; // Hitachi H8S
static const Elf_Half EM_H8_500      = 49; // Hitachi H8/500
static const Elf_Half EM_IA_64       = 50; // Intel Merced
static const Elf_Half EM_MIPS_X      = 51; // Stanford MIPS-X
static const Elf_Half EM_COLDFIRE    = 52; // Motorola Coldfire
static const Elf_Half EM_68HC12      = 53; // Motorola M68HC12
static const Elf_Half EM_OR1K        = 92; // OpenRISC 1000

// unofficial EM_* values
static const Elf_Half EM_ALPHA       = 0x9026; // Alpha
static const Elf_Half EM_MTALPHA     = 0xafef; // Microthreaded Alpha
static const Elf_Half EM_MTSPARC     = 0xaff0; // Microthreaded Sparc V8

#if defined(TARGET_MTALPHA)
#define MACHINE_NORMAL EM_MTALPHA
#define MACHINE_LEGACY EM_ALPHA
#elif defined(TARGET_MTSPARC)
#define MACHINE_NORMAL EM_MTSPARC
#define MACHINE_LEGACY EM_SPARC
#elif defined(TARGET_MIPS32) || defined(TARGET_MIPS32EL)
#define MACHINE_NORMAL EM_MIPS /* no MT for now */
#define MACHINE_LEGACY EM_MIPS
#elif defined(TARGET_OR1K)
#define MACHINE_NORMAL EM_OR1K /* no MT for now */
#define MACHINE_LEGACY EM_OR1K
#endif

// File header
#pragma pack(1)
struct Elf_Ehdr
{
	unsigned char e_ident[EI_NIDENT];
	Elf_Half	  e_type;
	Elf_Half	  e_machine;
	Elf_Word	  e_version;
	Elf_Addr	  e_entry;
	Elf_Off	      e_phoff;
	Elf_Off	      e_shoff;
	Elf_Word	  e_flags;
	Elf_Half	  e_ehsize;
	Elf_Half	  e_phentsize;
	Elf_Half	  e_phnum;
	Elf_Half	  e_shentsize;
	Elf_Half	  e_shnum;
	Elf_Half	  e_shstrndx;
};
#pragma pack()

static const Elf_Word PT_NULL    = 0;
static const Elf_Word PT_LOAD    = 1;
static const Elf_Word PT_DYNAMIC = 2;
static const Elf_Word PT_INTERP  = 3;
static const Elf_Word PT_NOTE    = 4;
static const Elf_Word PT_SHLIB   = 5;
static const Elf_Word PT_PHDR    = 6;
static const Elf_Word PT_LOPROC  = 0x70000000;
static const Elf_Word PT_HIPROC  = 0x7fffffff;

static const Elf_Word PF_R = 4;
static const Elf_Word PF_W = 2;
static const Elf_Word PF_X = 1;

// Program & section headers
#pragma pack(1)
#if ELFCLASS == ELFCLASS64
struct Elf_Phdr
{
	Elf_Word  p_type;
	Elf_Word  p_flags;
	Elf_Off   p_offset;
	Elf_Addr  p_vaddr;
	Elf_Addr  p_paddr;
	Elf_Xword p_filesz;
	Elf_Xword p_memsz;
	Elf_Xword p_align;
};

struct Elf_Shdr
{
    Elf_Word  sh_name;
    Elf_Word  sh_type;
    Elf_Xword sh_flags;
    Elf_Addr  sh_addr;
    Elf_Off   sh_offset;
    Elf_Xword sh_size;
    Elf_Word  sh_link;
    Elf_Word  sh_info;
    Elf_Xword sh_addralign;
    Elf_Xword sh_entsize;
};

struct Elf_Sym {
    Elf_Word      st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf_Half      st_shndx;
    Elf_Addr      st_value;
    Elf_Xword     st_size;
};

#else
struct Elf_Phdr
{
	Elf_Word  p_type;
	Elf_Off   p_offset;
	Elf_Addr  p_vaddr;
	Elf_Addr  p_paddr;
	Elf_Xword p_filesz;
	Elf_Xword p_memsz;
	Elf_Word  p_flags;
	Elf_Xword p_align;
};

struct Elf_Shdr
{
    Elf_Word sh_name;
    Elf_Word sh_type;
    Elf_Word sh_flags;
    Elf_Addr sh_addr;
    Elf_Off  sh_offset;
    Elf_Word sh_size;
    Elf_Word sh_link;
    Elf_Word sh_info;
    Elf_Word sh_addralign;
    Elf_Word sh_entsize;
};

struct Elf_Sym
{
    Elf_Word      st_name;
    Elf_Addr      st_value;
    Elf_Word      st_size;
    unsigned char st_info;
    unsigned char st_other;
    Elf_Half      st_shndx;
};

#endif
#pragma pack()

#define ELF_ST_BIND(val)                (((unsigned int)(val)) >> 4)
#define ELF_ST_TYPE(val)                ((val) & 0xF)
#define ELF_ST_INFO(bind,type)          (((bind) << 4) + ((type) & 0xF))

static const unsigned STN_UNDEF     = 0;  /* Undefined symbol index */

static const unsigned STB_LOCAL     = 0;  /* Symbol not visible outside obj */
static const unsigned STB_GLOBAL    = 1;  /* Symbol visible outside obj */
static const unsigned STB_WEAK      = 2;  /* Like globals, lower precedence */
static const unsigned STB_LOOS      = 10; /* OS-specific semantics */
static const unsigned STB_HIOS      = 12; /* OS-specific semantics */
static const unsigned STB_LOPROC    = 13; /* Application-specific semantics */
static const unsigned STB_HIPROC    = 15; /* Application-specific semantics */

static const unsigned STT_NOTYPE    = 0;  /* Symbol type is unspecified */
static const unsigned STT_OBJECT    = 1;  /* Symbol is a data object */
static const unsigned STT_FUNC      = 2;  /* Symbol is a code object */
static const unsigned STT_SECTION   = 3;  /* Symbol associated with a section */
static const unsigned STT_FILE      = 4;  /* Symbol gives a file name */
static const unsigned STT_COMMON    = 5;  /* An uninitialised common block */
static const unsigned STT_TLS       = 6;  /* Thread local data object */
static const unsigned STT_RELC      = 8;  /* Complex relocation expression */
static const unsigned STT_SRELC     = 9;  /* Signed Complex relocation expression */
static const unsigned STT_LOOS      = 10; /* OS-specific semantics */
static const unsigned STT_HIOS      = 12; /* OS-specific semantics */
static const unsigned STT_LOPROC    = 13; /* Application-specific semantics */
static const unsigned STT_HIPROC    = 15; /* Application-specific semantics */

static const Elf_Word SHT_NULL      = 0;  /* Section header table entry unused */
static const Elf_Word SHT_PROGBITS  = 1;  /* Program specific (private) data */
static const Elf_Word SHT_SYMTAB    = 2;  /* Link editing symbol table */
static const Elf_Word SHT_STRTAB    = 3;  /* A string table */
static const Elf_Word SHT_RELA      = 4;  /* Relocation entries with addends */
static const Elf_Word SHT_HASH      = 5;  /* A symbol hash table */
static const Elf_Word SHT_DYNAMIC   = 6;  /* Information for dynamic linking */
static const Elf_Word SHT_NOTE      = 7;  /* Information that marks file */
static const Elf_Word SHT_NOBITS    = 8;  /* Section occupies no space in file */
static const Elf_Word SHT_REL       = 9;  /* Relocation entries, no addends */
static const Elf_Word SHT_SHLIB     = 10; /* Reserved, unspecified semantics */
static const Elf_Word SHT_DYNSYM    = 11; /* Dynamic linking symbol table */

#endif

