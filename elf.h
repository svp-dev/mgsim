#ifndef ELF_H
#define ELF_H

// ELF types
#include "types.h"

// Some ELF types
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
static const unsigned char ELFCLASSNONE = 0; // Invalid class
static const unsigned char ELFCLASS32   = 1; // 32-bit objects
static const unsigned char ELFCLASS64   = 2; // 64-bit objects

// e_ident[EI_DATA]
static const unsigned char ELFDATANONE = 0; // Invalid data encoding
static const unsigned char ELFDATA2LSB = 1; // 2's complement values, with the LSB occupying the lowest address.
static const unsigned char ELFDATA2MSB = 2; // 2's complement values, with the MSB occupying the lowest address.

// e_ident[EI_VERSION]
static const unsigned char EV_NONE    = 0; // Invalid version
static const unsigned char EV_CURRENT = 1; // Current version

static const Elf64_Half ET_NONE   = 0; // No file type
static const Elf64_Half ET_REL    = 1; // Relocatable file
static const Elf64_Half ET_EXEC   = 2; // Executable file
static const Elf64_Half ET_DYN    = 3; // Shared object file
static const Elf64_Half ET_CORE   = 4; // Core file
static const Elf64_Half ET_LOPROC = 0xff00; // Processor-specific
static const Elf64_Half ET_HIPROC = 0xffff; // Processor-specific

// Legal values for e_machine (architecture)
static const Elf64_Half EM_NONE        =  0; // No machine
static const Elf64_Half EM_M32         =  1; // AT&T WE 32100
static const Elf64_Half EM_SPARC       =  2; // SPARC
static const Elf64_Half EM_386         =  3; // Intel 80386
static const Elf64_Half EM_68K         =  4; // Motorola 68000
static const Elf64_Half EM_88K         =  5; // Motorola 88000
static const Elf64_Half EM_860         =  7; // Intel 80860
static const Elf64_Half EM_MIPS        =  8; // MIPS R3000 big-endian
static const Elf64_Half EM_S370        =  9; // Amdahl
static const Elf64_Half EM_MIPS_RS4_BE = 10; // MIPS R4000 big-endian
static const Elf64_Half EM_RS6000      = 11; // RS6000

static const Elf64_Half EM_PARISC      = 15; // HPPA
static const Elf64_Half EM_nCUBE       = 16; // nCUBE
static const Elf64_Half EM_VPP500      = 17; // Fujitsu VPP500
static const Elf64_Half EM_SPARC32PLUS = 18; // Sun's "v8plus"
static const Elf64_Half EM_960         = 19; // Intel 80960
static const Elf64_Half EM_PPC         = 20; // PowerPC

static const Elf64_Half EM_V800        = 36; // NEC V800 series
static const Elf64_Half EM_FR20        = 37; // Fujitsu FR20
static const Elf64_Half EM_RH32        = 38; // TRW RH32
static const Elf64_Half EM_MMA         = 39; // Fujitsu MMA
static const Elf64_Half EM_ARM         = 40; // ARM
static const Elf64_Half EM_FAKE_ALPHA  = 41; // Digital Alpha
static const Elf64_Half EM_SH          = 42; // Hitachi SH
static const Elf64_Half EM_SPARCV9     = 43; // SPARC v9 64-bit
static const Elf64_Half EM_TRICORE     = 44; // Siemens Tricore
static const Elf64_Half EM_ARC         = 45; // Argonaut RISC Core
static const Elf64_Half EM_H8_300      = 46; // Hitachi H8/300
static const Elf64_Half EM_H8_300H     = 47; // Hitachi H8/300H
static const Elf64_Half EM_H8S         = 48; // Hitachi H8S
static const Elf64_Half EM_H8_500      = 49; // Hitachi H8/500
static const Elf64_Half EM_IA_64       = 50; // Intel Merced
static const Elf64_Half EM_MIPS_X      = 51; // Stanford MIPS-X
static const Elf64_Half EM_COLDFIRE    = 52; // Motorola Coldfire
static const Elf64_Half EM_68HC12      = 53; // Motorola M68HC12

// unofficial EM_* values
static const Elf64_Half EM_ALPHA       = 0x9026; // Alpha

// File header
struct Elf64_Ehdr
{
	unsigned char e_ident[EI_NIDENT];
	Elf64_Half	  e_type;
	Elf64_Half	  e_machine;
	Elf64_Word	  e_version;
	Elf64_Addr	  e_entry;
	Elf64_Off	  e_phoff;
	Elf64_Off	  e_shoff;
	Elf64_Word	  e_flags;
	Elf64_Half	  e_ehsize;
	Elf64_Half	  e_phentsize;
	Elf64_Half	  e_phnum;
	Elf64_Half	  e_shentsize;
	Elf64_Half	  e_shnum;
	Elf64_Half	  e_shstrndx;
};

static const Elf64_Word PT_NULL    = 0;
static const Elf64_Word PT_LOAD    = 1;
static const Elf64_Word PT_DYNAMIC = 2;
static const Elf64_Word PT_INTERP  = 3;
static const Elf64_Word PT_NOTE    = 4;
static const Elf64_Word PT_SHLIB   = 5;
static const Elf64_Word PT_PHDR    = 6;
static const Elf64_Word PT_LOPROC  = 0x70000000;
static const Elf64_Word PT_HIPROC  = 0x7fffffff;

static const Elf64_Word PF_R = 4;
static const Elf64_Word PF_W = 2;
static const Elf64_Word PF_X = 1;

// Program header
struct Elf64_Phdr
{
	Elf64_Word  p_type;
	Elf64_Word  p_flags;
	Elf64_Off   p_offset;
	Elf64_Addr  p_vaddr;
	Elf64_Addr  p_paddr;
	Elf64_Xword p_filesz;
	Elf64_Xword p_memsz;
	Elf64_Xword p_align;
};

#endif

