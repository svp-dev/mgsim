#include "RPC_unix.h"
#include "RPCServiceDatabase.h"
#include <algorithm>
#include <cerrno>
#include <iomanip>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>

using namespace std;

namespace Simulator
{

    UnixInterface::UnixInterface(const string& name, Object& parent)
        : Object(name, parent),
          m_vfds(1),
          m_nrequests(0),
          m_nfailures(0),
          m_nstats(0),
          m_nreads(0),
          m_nread_bytes(0),
          m_nwrites(0),
          m_nwrite_bytes(0)
    {
        RegisterSampleVariableInObject(m_nrequests, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_nfailures, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_nstats, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_nreads, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_nread_bytes, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_nwrites, SVC_CUMULATIVE);
        RegisterSampleVariableInObject(m_nwrite_bytes, SVC_CUMULATIVE);
    }

    UnixInterface::VirtualDescriptor* UnixInterface::GetEntry(UnixInterface::VirtualFD vfd)
    {
        if (vfd >= m_vfds.size() || !m_vfds[vfd].active)
            return NULL;
        return &m_vfds[vfd];
    }

    UnixInterface::VirtualFD UnixInterface::GetNewVFD(const string& fname, UnixInterface::HostFD hfd)
    {
        size_t i;
        for (i = 0; i < m_vfds.size() && m_vfds[i].active; ++i)
            /* loop */;
        if (i == m_vfds.size())
            m_vfds.resize(m_vfds.size() + m_vfds.size() / 2);
        m_vfds[i].active = true;
        m_vfds[i].hfd = hfd;
        m_vfds[i].dir = 0;
        m_vfds[i].cycle_open = GetKernel()->GetCycleNo();
        m_vfds[i].cycle_use = m_vfds[i].cycle_open;
        return i;
    }

    UnixInterface::VirtualFD UnixInterface::DuplicateVFD(UnixInterface::VirtualFD original, UnixInterface::HostFD new_hfd)
    {
        VirtualDescriptor *vd = GetEntry(original);
        assert(vd != NULL);
        return GetNewVFD(vd->fname, new_hfd);
    }

    UnixInterface::VirtualDescriptor* UnixInterface::DuplicateVFD2(UnixInterface::VirtualFD original, UnixInterface::VirtualFD target)
    {
        VirtualDescriptor *vd = GetEntry(original);
        assert(vd != NULL);

        if (target >= m_vfds.size())
            m_vfds.resize(target + 1);

        if (target != original)
            m_vfds[target].dir = 0;

        return &m_vfds[target];
    }

#define RequireArgs(Arg1Size, Arg2Size)                                 \
    do {                                                                \
        if ((Arg1Size) && arg1.size() < (Arg1Size))                     \
            throw exceptf<SimulationException>("Procedure %u requires at least %zu words in 1st memory argument, %zu were provided", \
                                               (unsigned)procedure_id,  \
                                               (size_t)(Arg1Size),      \
                                               arg1.size());            \
        if ((Arg2Size) && arg2.size() < (Arg2Size))                     \
            throw exceptf<SimulationException>("Procedure %u requires at least %zu words in 2nd memory argument, %zu were provided", \
                                               (unsigned)procedure_id,  \
                                               (size_t)(Arg2Size),      \
                                               arg2.size());            \
    } while(0)

    void UnixInterface::Service(uint32_t procedure_id,
                                vector<uint32_t>& res1, size_t res1_maxsize,
                                vector<uint32_t>& res2, size_t res2_maxsize,
                                const vector<uint32_t>& arg1,
                                const vector<uint32_t>& arg2,
                                uint32_t arg3,
                                uint32_t arg4)
    {
        if (res1_maxsize < 3)
        {
            throw exceptf<SimulationException>("Procedure %u requires at least 3words available in 1st memory region", (unsigned)procedure_id);
        }

        uint64_t rval = 0;
        errno = 0;

        switch(procedure_id)
        {
        case RPC_nop:
            // this syscall can be used to check the interface works.
            break;

        case RPC_read:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }

            size_t sz = arg4;

            if (sz > res2_maxsize * 4)
                sz = res2_maxsize * 4;
            res2.resize((sz + 4 - 1) / 4);

            ssize_t s = read(vd->hfd, &res2[0], sz);
            if (s >= 0)
            {
                res2.resize((s + 4 - 1) / 4);

                ++m_nreads;
                m_nread_bytes += s;
            }

            rval = s;

        }
        break;

        case RPC_write:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }

            size_t sz = arg4;

            if (sz > arg2.size() * 4)
                sz = arg2.size() * 4;

            ssize_t s = write(vd->hfd, &arg2[0], sz);

            if (s >= 0)
            {
                ++m_nwrites;
                m_nwrite_bytes += s;
            }

            rval = s;
        }
        break;

        case RPC_open:
        {
            int iflags = arg3;
            int mode = arg4;
            int oflags;

            // translate virtual flags (iflags) to real flags (oflags)
            if      ((iflags & VO_ACCMODE) == VO_RDONLY) oflags = O_RDONLY;
            else if ((iflags & VO_ACCMODE) == VO_WRONLY) oflags = O_WRONLY;
            else if ((iflags & VO_ACCMODE) == VO_RDWR)   oflags = O_RDWR;
            iflags ^= VO_ACCMODE;

            if (iflags & VO_APPEND)   { oflags |= O_APPEND;   iflags ^= O_APPEND; }
            if (iflags & VO_NOFOLLOW) { oflags |= O_NOFOLLOW; iflags ^= O_APPEND; }
            if (iflags & VO_CREAT)    { oflags |= O_CREAT;    iflags ^= O_CREAT;  }
            if (iflags & VO_TRUNC)    { oflags |= O_TRUNC;    iflags ^= O_TRUNC;  }
            if (iflags & VO_EXCL)     { oflags |= O_EXCL;     iflags ^= O_EXCL;   }

            if (iflags != 0)
            {
                rval = -1;
                errno = EINVAL;
                break;
            }

            // ensure path is nul terminated
            const char *path_start = (const char*)(const void*)&arg2[0];
            const char *path_end = path_start + arg2.size()*4;
            int fd;
            if (find(path_start, path_end, '\0') == path_end)
            {
                rval = -1;
                errno = ENAMETOOLONG;
                break;
            }

            int hfd = open(path_start, oflags, mode);

            rval = GetNewVFD(path_start, hfd);
        }
        break;

        case RPC_close:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
            if (vd->dir)
                rval = closedir(vd->dir);
            else
                rval = close(vd->hfd);

            if (rval == 0)
                vd->active = false;
        }
        break;

        case RPC_link:
        {
            RequireArgs(1, 1);
            // ensure paths are nul terminated
            const char *path1_start = (const char*)(const void*)&arg1[0];
            const char *path1_end = path1_start + arg1.size()*4;
            const char *path2_start = (const char*)(const void*)&arg2[0];
            const char *path2_end = path2_start + arg2.size()*4;
            if (find(path1_start, path1_end, '\0') == path1_end ||
                find(path2_start, path2_end, '\0') == path2_end)
            {
                rval = -1;
                errno = ENAMETOOLONG;
                break;
            }

            rval = link(path1_start, path2_start);
        }
        break;

        case RPC_unlink:
        {
            RequireArgs(1, 0);
            // ensure path is nul terminated
            const char *path_start = (const char*)(const void*)&arg1[0];
            const char *path_end = path_start + arg1.size()*4;
            if (find(path_start, path_end, '\0') == path_end)
            {
                errno = ENAMETOOLONG;
                rval = -1;
            }

            rval = unlink(path_start);
        }
        break;

        case RPC_sync:
            rval = 0;
            errno = 0;
            sync();
            break;

        case RPC_dup:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
            int new_hfd = dup(vd->hfd);
            rval = DuplicateVFD(arg3, new_hfd);
        }
        break;

        case RPC_dup2:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
            VirtualDescriptor *vd2 = DuplicateVFD2(arg3, arg4);
            int new_hfd;
            if (vd2->active)
                new_hfd = dup2(vd->hfd, vd2->hfd);
            else
                new_hfd = dup(vd->hfd);

            if (new_hfd != -1)
            {
                rval = arg4;
                *vd2 = *vd;
                vd2->hfd = new_hfd;
            }
            else
            {
                rval = -1;
                vd2->active = false;
            }
        }
        break;

        case RPC_getdtablesize:
            errno = 0;
#ifdef HAVE_GETDTABLESIZE
            rval = getdtablesize();
#else
            rval = 17; // POSIX says minimum 20, minus the simulator's stdin/stdout/stderr
#endif
            break;

        case RPC_fsync:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
#ifdef HAVE_FSYNC
            rval = fsync(vd->hfd);
#else
            sync();
            rval = 0;
#endif
        }
        break;

        case RPC_rename:
        {
            RequireArgs(1, 1);
            // ensure paths are nul terminated
            const char *path1_start = (const char*)(const void*)&arg1[0];
            const char *path1_end = path1_start + arg1.size()*4;
            const char *path2_start = (const char*)(const void*)&arg2[0];
            const char *path2_end = path2_start + arg2.size()*4;
            if (find(path1_start, path1_end, '\0') == path1_end ||
                find(path2_start, path2_end, '\0') == path2_end)
            {
                rval = -1;
                errno = ENAMETOOLONG;
                break;
            }

            rval = rename(path1_start, path2_start);
        }
        break;

        case RPC_mkdir:
        {
            RequireArgs(1, 0);
            // ensure paths are nul terminated
            const char *path_start = (const char*)(const void*)&arg1[0];
            const char *path_end = path_start + arg1.size()*4;
            if (find(path_start, path_end, '\0') == path_end)
            {
                rval = -1;
                errno = ENAMETOOLONG;
                break;
            }

            rval = mkdir(path_start, arg3);
        }
        break;

        case RPC_rmdir:
        {
            RequireArgs(1, 0);
            // ensure paths are nul terminated
            const char *path_start = (const char*)(const void*)&arg1[0];
            const char *path_end = path_start + arg1.size()*4;
            if (find(path_start, path_end, '\0') == path_end)
            {
                rval = -1;
                errno = ENAMETOOLONG;
                break;
            }

            rval = rmdir(path_start);
        }
        break;

        case RPC_pread:
        {
            RequireArgs(2, 0);
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }

            off_t offset = arg1[0] | ((off_t)arg1[1] << 32);
            size_t sz = arg4;

            if (sz > res2_maxsize * 4)
                sz = res2_maxsize * 4;
            res2.resize((sz + 4 - 1) / 4);

            ssize_t s = pread(vd->hfd, &res2[0], sz, offset);
            if (s >= 0)
            {
                res2.resize((s + 4 - 1) / 4); // round up

                ++m_nreads;
                m_nread_bytes += s;
            }


            rval = s;
        }
        break;

        case RPC_pwrite:
        {
            RequireArgs(2, 0);
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }

            off_t offset = arg1[0] | ((off_t)arg1[1] << 32);
            size_t sz = arg4;

            if (sz > arg2.size() * 4)
                sz = arg2.size() * 4;
            
            ssize_t s = pwrite(vd->hfd, &arg2[0], sz, offset);

            if (s >= 0)
            {
                ++m_nwrites;
                m_nwrite_bytes += s;
            }

            rval = s;
        }
        break;

        case RPC_stat:
        case RPC_lstat:
        case RPC_fstat:
        {
            struct stat st;
            switch(procedure_id)
            {
            case RPC_fstat:
            {
                VirtualDescriptor *vd = GetEntry(arg3);
                if (vd == NULL)
                {
                    errno = EBADF;
                    rval = -1;
                    break;
                }
                rval = fstat(vd->hfd, &st);
            }
            break;
            case RPC_stat:
            case RPC_lstat:
            {
                RequireArgs(1, 0);
                // ensure paths are nul terminated
                const char *path_start = (const char*)(const void*)&arg1[0];
                const char *path_end = path_start + arg1.size()*4;
                if (find(path_start, path_end, '\0') == path_end)
                {
                    rval = -1;
                    errno = ENAMETOOLONG;
                    break;
                }

                if (procedure_id == RPC_stat)
                    rval = stat(path_start, &st);
                else
                    rval = lstat(path_start, &st);
            }
            break;
            }

            if (rval == 0)
            {
                res2.resize(sizeof(struct vstat) / 4);
                struct vstat *vst = (struct vstat*)(void*)&res2[0];
                vst->vst_dev = st.st_dev;
                vst->vst_ino_low = st.st_ino & 0xffffffffUL;
                vst->vst_ino_high = (st.st_ino >> 32) & 0xffffffffUL;

                if      ((st.st_mode & S_IFMT) == S_IFDIR) vst->vst_mode = VS_IFDIR;
                else if ((st.st_mode & S_IFMT) == S_IFREG) vst->vst_mode = VS_IFREG;
                else if ((st.st_mode & S_IFMT) == S_IFLNK) vst->vst_mode = VS_IFLNK;
                else                                     vst->vst_mode = VS_IFUNKNOWN;

                vst->vst_nlink = st.st_nlink;
#ifdef HAVE_STRUCT_STAT_ST_ATIMESPEC
                vst->vst_atime_secs = st.st_atimespec.tv_sec;
                vst->vst_atime_nsec = st.st_atimespec.tv_nsec;
#else
                vst->vst_atime_secs = st.st_atime;
                vst->vst_atime_nsec = 0;
#endif
#ifdef HAVE_STRUCT_STAT_ST_CTIMESPEC
                vst->vst_ctime_secs = st.st_ctimespec.tv_sec;
                vst->vst_ctime_nsec = st.st_ctimespec.tv_nsec;
#else
                vst->vst_ctime_secs = st.st_ctime;
                vst->vst_ctime_nsec = 0;
#endif
#ifdef HAVE_STRUCT_STAT_ST_MTIMESPEC
                vst->vst_mtime_secs = st.st_mtimespec.tv_sec;
                vst->vst_mtime_nsec = st.st_mtimespec.tv_nsec;
#else
                vst->vst_mtime_secs = st.st_mtime;
                vst->vst_mtime_nsec = 0;
#endif

                vst->vst_size_low = st.st_size & 0xffffffffUL;
                vst->vst_size_high = (st.st_size >> 32) & 0xffffffffUL;
#if HAVE_STRUCT_STAT_ST_BLOCKS
                vst->vst_blocks_low = st.st_blocks & 0xffffffffUL;
                vst->vst_blocks_high = (st.st_blocks >> 32) & 0xffffffffUL;
#else
                vst->vst_blocks_low = vst->vst_blocks_high = 0;
#endif
#if HAVE_STRUCT_STAT_ST_BLKSIZE
                vst->vst_blksize = st.st_blksize;
#else
                vst->vst_blksize = 0;
#endif

                ++m_nstats;

            }
        }
        break;

        case RPC_opendir:
        {
            RequireArgs(1, 0);
            // ensure paths are nul terminated
            const char *path_start = (const char*)(const void*)&arg1[0];
            const char *path_end = path_start + arg1.size()*4;
            if (find(path_start, path_end, '\0') == path_end)
            {
                rval = -1;
                errno = ENAMETOOLONG;
                break;
            }

            DIR* dir = opendir(path_start);
            if (dir != NULL)
            {
                VirtualFD vfd = GetNewVFD(path_start, dirfd(dir));
                VirtualDescriptor *vd = GetEntry(vfd);
                assert(vd != NULL);
                vd->dir = dir;
                rval = vfd;
            }
            else
            {
                rval = -1;
            }
        }
        break;

        case RPC_fdopendir:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }

            if (vd->dir != NULL)
            {
                rval = 0;
                errno = EBUSY;
                break;
            }

#ifdef HAVE_FDOPENDIR
            DIR *dir = fdopendir(vd->hfd);
#else
            DIR *dir = opendir(vd->fname.c_str());
#endif
            if (dir != NULL)
            {
                vd->dir = dir;
                rval = arg3;
            }
            else
                rval = 0;
        }
        break;

        case RPC_rewinddir:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL || vd->dir == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
            rewinddir(vd->dir);
            rval = 0;
        }
        break;

        case RPC_telldir:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL || vd->dir == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
            rval = telldir(vd->dir);
        }
        break;

        case RPC_seekdir:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL || vd->dir == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
            seekdir(vd->dir, arg4);
            rval = 0;
        }
        break;

        case RPC_closedir:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL || vd->dir == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
            rval = closedir(vd->dir);
            if (rval == 0)
                vd->active = false;
        }
        break;

        case RPC_readdir:
        {
            VirtualDescriptor *vd = GetEntry(arg3);
            if (vd == NULL || vd->dir == NULL)
            {
                errno = EBADF;
                rval = -1;
                break;
            }
            struct dirent *de = readdir(vd->dir);
            if (de != NULL)
            {
                size_t namlen = strlen(de->d_name) & 0xffff;
                size_t bytes_needed = sizeof(struct vdirent) + namlen + 1 /* extra nul byte */;
                size_t words_needed = (bytes_needed + 4 - 1) / 4; // round up
                if (words_needed > res2_maxsize)
                {
                    errno = EOVERFLOW;
                    rval = -1;
                    break;
                }

                res2.resize(words_needed);

                struct vdirent *vde = (struct vdirent*)(void*)&res2;

#ifdef HAVE_STRUCT_DIRENT_D_INO
                vde->vd_ino_low = de->d_ino & 0xffffffffUL;
                vde->vd_ino_high = (de->d_ino >> 32) & 0xffffffffUL;
#else
                vde->vd_ino_low = vde->vd_ino_high = 0;
#endif

#if HAVE_STRUCT_DIRENT_D_TYPE
                if      (de->d_type == DT_DIR) vde->vd_type_namlen = VDT_DIR;
                else if (de->d_type == DT_REG) vde->vd_type_namlen = VDT_REG;
                else if (de->d_type == DT_LNK) vde->vd_type_namlen = VDT_LNK;
                else
#endif
                    vde->vd_type_namlen = VDT_UNKNOWN;

                vde->vd_type_namlen |= namlen << 16;
                memcpy(vde->vd_name, de->d_name, namlen);
                vde->vd_name[namlen] = '\0';

                rval = 1;
            }
            else
                rval = 0;
        }
        break;

        default:
            errno = ENOSYS;
        }



        // all unix calls returns a 64-bit value in little-endian format,
        // followed by 32-bit errno.
        res1.resize(3);
        res1[0] = rval & 0xffffffffUL;
        res1[1] = (rval >> 32) & 0xffffffffUL;
        res1[2] = errno;

        ++m_nrequests;
        if (errno != 0)
            ++m_nfailures;
    }

#undef RequireArgs

    void UnixInterface::Cmd_Info(ostream& out, const vector<string>& /*args*/) const
    {
        out << "The Unix interface provides a reduced POSIX interface to the host system." << endl
            << endl;
        bool some = false;
        for (size_t i = 0; i < m_vfds.size(); ++i)
            if (m_vfds[i].active)
            {
                some = true;
                break;
            }
        if (!some)
            out << "(no virtual handles are currently defined)" << endl;
        else
        {
            out << "VFD | HFD | Opened    | Last use  | DIR? | Path" << endl
                << "----+-----+-----------+-----------+------+-----" << endl
                << dec;
            for (size_t i = 0; i < m_vfds.size(); ++i)
            {
                if (!m_vfds[i].active)
                    continue;
                out << setw(3) << setfill(' ') << i
                    << " | "
                    << setw(3) << setfill(' ') << m_vfds[i].hfd
                    << " | "
                    << setw(10) << setfill(' ') << m_vfds[i].cycle_open
                    << " | "
                    << setw(10) << setfill(' ') << m_vfds[i].cycle_use
                    << " | "
                    << (m_vfds[i].dir == 0 ? "yes " : "no  ")
                    << " | "
                    << m_vfds[i].fname
                    << endl;
            }
        }
    }

}
