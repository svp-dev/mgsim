#ifndef RPCSERVICEDATABASE_H
#define RPCSERVICEDATABASE_H

/* low values (from 0 onwards) mimic standard Unix calls */
#define RPC_nop            0

#define RPC_read           3
#define RPC_write          4
#define	RPC_open           5
#define	RPC_close          6

#define	RPC_link           9
#define	RPC_unlink         10

#define	RPC_sync           36

#define	RPC_dup            41

#define	RPC_symlink        57
#define	RPC_readlink       58

#define	RPC_getdtablesize  89
#define	RPC_dup2           90

#define	RPC_fcntl          92

#define	RPC_fsync          95

#define	RPC_rename         128

#define	RPC_mkdir          136
#define	RPC_rmdir          137

#define	RPC_pread          153
#define	RPC_pwrite         154

#define	RPC_stat           188
#define	RPC_fstat          189
#define	RPC_lstat          190

#define	RPC_lseek          199
#define	RPC_truncate       200
#define	RPC_ftruncate      201

#define	RPC_opendir        300
#define RPC_fdopendir      301
#define	RPC_readdir        302
#define	RPC_telldir        303
#define	RPC_seekdir        304
#define	RPC_rewinddir      305
#define	RPC_closedir       306



// flags for RPC_open
#define VO_RDONLY   1
#define VO_WRONLY   2
#define VO_RDWR     3
#define VO_ACCMODE  3
#define VO_APPEND   8
#define VO_NOFOLLOW 0x100
#define VO_CREAT    0x200
#define VO_TRUNC    0x400
#define VO_EXCL     0x800

// struct stat for stat/lstat/fstat
struct vstat
{
    uint32_t vst_dev;
    uint32_t vst_ino_low;
    uint32_t vst_ino_high;
    uint32_t vst_mode;
    uint32_t vst_nlink;
    uint32_t vst_atime_secs;
    uint32_t vst_atime_nsec;
    uint32_t vst_mtime_secs;
    uint32_t vst_mtime_nsec;
    uint32_t vst_ctime_secs;
    uint32_t vst_ctime_nsec;
    uint32_t vst_size_low;
    uint32_t vst_size_high;
    uint32_t vst_blocks_low;
    uint32_t vst_blocks_high;
    uint32_t vst_blksize;
};

// values for vstat.vst_mode
#define VS_IFMT      0170000  /* type of file mask */
#define VS_IFUNKNOWN 0000000  /* unknown */
#define VS_IFDIR     0040000  /* directory */
#define VS_IFREG     0100000  /* regular */
#define VS_IFLNK     0120000  /* symbolic link */

// struct dirent for readdir
struct vdirent
{
    uint32_t vd_ino_low;
    uint32_t vd_ino_high;
    uint32_t vd_type_namlen; // low 16 bits: vd_type; high 16 bits: vd_namlen;
    char     vd_name[0];
};

// values for 16 low order bits of vdirent.vd_type_namlen
#define VDT_UNKNOWN       0
#define VDT_DIR           4
#define VDT_REG           8
#define VDT_LNK          10



#endif
